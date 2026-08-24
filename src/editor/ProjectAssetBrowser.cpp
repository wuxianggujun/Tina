#include <tina/editor/ProjectAssetBrowser.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace Tina::Editor {
namespace {

constexpr Core::usize MaxProjectAssetFolderPathBytes = 4096U;
constexpr Core::usize MaxProjectAssetFolderCount = 65536U;

[[nodiscard]] bool validTypeFilter(ProjectAssetTypeFilter filter) noexcept
{
    switch (filter)
    {
    case ProjectAssetTypeFilter::All:
    case ProjectAssetTypeFilter::Images:
    case ProjectAssetTypeFilter::Models:
    case ProjectAssetTypeFilter::Scenes:
    case ProjectAssetTypeFilter::Audio:
    case ProjectAssetTypeFilter::Animation:
    case ProjectAssetTypeFilter::Other:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool validDependencyFlags(AssetFormat::DependencyFlags flags) noexcept
{
    constexpr auto Required = static_cast<Core::u16>(AssetFormat::DependencyFlags::Required);
    constexpr auto Deferred = static_cast<Core::u16>(AssetFormat::DependencyFlags::Deferred);
    const auto value = static_cast<Core::u16>(flags);
    return (value & Required) != 0U && (value & ~(Required | Deferred)) == 0U;
}

[[nodiscard]] char asciiFold(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] bool containsAsciiInsensitive(std::string_view text,
                                            std::string_view query) noexcept
{
    if (query.empty())
    {
        return true;
    }
    if (query.size() > text.size())
    {
        return false;
    }
    for (Core::usize start = 0; start + query.size() <= text.size(); ++start)
    {
        bool match = true;
        for (Core::usize offset = 0; offset < query.size(); ++offset)
        {
            if (asciiFold(text[start + offset]) != asciiFold(query[offset]))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Core::Result<std::string>
normalizeFolderPath(std::string_view folderPathUtf8)
{
    if (folderPathUtf8.empty())
    {
        return std::string{};
    }
    if (folderPathUtf8.size() > MaxProjectAssetFolderPathBytes ||
        !Core::isStrictUtf8WithoutNul(folderPathUtf8) ||
        folderPathUtf8.front() == '/' || folderPathUtf8.front() == '\\' ||
        folderPathUtf8.back() == '/' || folderPathUtf8.back() == '\\')
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Project asset folder path is invalid");
    }

    try
    {
        std::string normalized;
        normalized.reserve(folderPathUtf8.size());
        Core::usize componentBegin = 0U;
        while (componentBegin < folderPathUtf8.size())
        {
            const Core::usize separator = folderPathUtf8.find_first_of(
                "/\\", componentBegin);
            const Core::usize componentEnd =
                separator == std::string_view::npos ? folderPathUtf8.size() : separator;
            const std::string_view component =
                folderPathUtf8.substr(componentBegin, componentEnd - componentBegin);
            if (component.empty() || component == "." || component == ".." ||
                component.find(':') != std::string_view::npos ||
                component.find_first_of("\t\r\n") != std::string_view::npos)
            {
                return Core::failure(EditorErrorCode::InvalidConfiguration,
                                     "Project asset folder path has an invalid component");
            }
            if (!normalized.empty())
            {
                normalized.push_back('/');
            }
            normalized.append(component);
            if (separator == std::string_view::npos)
            {
                break;
            }
            componentBegin = separator + 1U;
        }
        return normalized;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Project asset folder normalization allocation failed");
    }
}

void appendFolderAndParents(std::vector<std::string>& paths,
                            std::string_view normalizedPath)
{
    Core::usize end = normalizedPath.find('/');
    while (end != std::string_view::npos)
    {
        paths.emplace_back(normalizedPath.substr(0U, end));
        end = normalizedPath.find('/', end + 1U);
    }
    if (!normalizedPath.empty())
    {
        paths.emplace_back(normalizedPath);
    }
}

[[nodiscard]] Core::Result<std::vector<ProjectAssetFolderDescriptor>>
buildFolderDescriptors(std::span<const ProjectAssetDescriptor> assets,
                       std::span<const std::string> scannedFolderPaths,
                       Core::usize folderCapacity)
{
    try
    {
        std::vector<std::string> paths;
        paths.reserve((std::min)(folderCapacity, assets.size()));
        for (const auto& asset : assets)
        {
            appendFolderAndParents(paths, asset.folderPathUtf8);
        }
        for (const auto& scannedPath : scannedFolderPaths)
        {
            auto normalized = normalizeFolderPath(scannedPath);
            if (!normalized)
            {
                return Core::failure(std::move(normalized.error()));
            }
            appendFolderAndParents(paths, *normalized);
        }
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        if (paths.size() + 1U > folderCapacity)
        {
            return Core::failure(EditorErrorCode::ProjectAssetCapacityExceeded,
                                 "Project Source exceeds the asset folder capacity");
        }

        std::vector<ProjectAssetFolderDescriptor> folders;
        folders.reserve(paths.size() + 1U);
        folders.push_back(ProjectAssetFolderDescriptor{
            .pathUtf8 = {},
            .displayName = "Assets",
            .level = 0U,
            .expandable = !paths.empty(),
        });
        for (Core::usize index = 0U; index < paths.size(); ++index)
        {
            const std::string& path = paths[index];
            const Core::usize finalSeparator = path.rfind('/');
            const std::string_view displayName =
                finalSeparator == std::string::npos
                    ? std::string_view{path}
                    : std::string_view{path}.substr(finalSeparator + 1U);
            const Core::u32 level = static_cast<Core::u32>(
                1U + static_cast<Core::usize>(std::count(path.begin(), path.end(), '/')));
            bool expandable = false;
            if (index + 1U < paths.size())
            {
                const std::string& next = paths[index + 1U];
                expandable = next.size() > path.size() &&
                             next.compare(0U, path.size(), path) == 0 &&
                             next[path.size()] == '/';
            }
            folders.push_back(ProjectAssetFolderDescriptor{
                .pathUtf8 = path,
                .displayName = std::string(displayName),
                .level = level,
                .expandable = expandable,
            });
        }
        return folders;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Project asset folder index allocation failed");
    }
}

[[nodiscard]] bool pathIsInFolder(std::string_view assetFolder,
                                  std::string_view currentFolder) noexcept
{
    return currentFolder.empty() || assetFolder == currentFolder ||
           (assetFolder.size() > currentFolder.size() &&
            assetFolder.compare(0U, currentFolder.size(), currentFolder) == 0 &&
            assetFolder[currentFolder.size()] == '/');
}

} // namespace

std::string_view projectAssetKindLabel(AssetFormat::AssetKind kind) noexcept
{
    switch (kind)
    {
    case AssetFormat::AssetKind::Texture2D:
        return "Texture2D";
    case AssetFormat::AssetKind::Shader:
        return "Shader";
    case AssetFormat::AssetKind::Font:
        return "Font";
    case AssetFormat::AssetKind::Sprite:
        return "Sprite";
    case AssetFormat::AssetKind::Tileset:
        return "Tileset";
    case AssetFormat::AssetKind::TileMap:
        return "TileMap";
    case AssetFormat::AssetKind::StaticMesh:
        return "StaticMesh";
    case AssetFormat::AssetKind::SkinnedMesh:
        return "SkinnedMesh";
    case AssetFormat::AssetKind::AnimationClip3D:
        return "Animation3D";
    case AssetFormat::AssetKind::Material:
        return "Material";
    case AssetFormat::AssetKind::Prefab:
        return "Prefab";
    case AssetFormat::AssetKind::AudioClip:
        return "AudioClip";
    case AssetFormat::AssetKind::SpriteAnimationClip:
        return "Animation";
    case AssetFormat::AssetKind::TileMapChunk:
        return "TileChunk";
    case AssetFormat::AssetKind::EnvironmentMap:
        return "Environment";
    case AssetFormat::AssetKind::NavigationGrid2D:
        return "Navigation2D";
    case AssetFormat::AssetKind::Fx2D:
        return "FX2D";
    case AssetFormat::AssetKind::Invalid:
    default:
        return "Invalid";
    }
}

ProjectAssetOpenKind projectAssetOpenKind(AssetFormat::AssetKind kind) noexcept
{
    switch (kind)
    {
    case AssetFormat::AssetKind::Prefab:
        return ProjectAssetOpenKind::World3D;
    case AssetFormat::AssetKind::TileMap:
        return ProjectAssetOpenKind::TileMap2D;
    case AssetFormat::AssetKind::SpriteAnimationClip:
        return ProjectAssetOpenKind::SpriteAnimation2D;
    default:
        return ProjectAssetOpenKind::AssetInspector;
    }
}

std::string_view projectAssetTypeFilterLabel(ProjectAssetTypeFilter filter) noexcept
{
    switch (filter)
    {
    case ProjectAssetTypeFilter::All:
        return "All Types";
    case ProjectAssetTypeFilter::Images:
        return "Images";
    case ProjectAssetTypeFilter::Models:
        return "Models";
    case ProjectAssetTypeFilter::Scenes:
        return "Scenes & Maps";
    case ProjectAssetTypeFilter::Audio:
        return "Audio";
    case ProjectAssetTypeFilter::Animation:
        return "Animation";
    case ProjectAssetTypeFilter::Other:
        return "Other";
    default:
        return "Invalid";
    }
}

bool projectAssetMatchesTypeFilter(AssetFormat::AssetKind kind,
                                   ProjectAssetTypeFilter filter) noexcept
{
    switch (filter)
    {
    case ProjectAssetTypeFilter::All:
        return kind != AssetFormat::AssetKind::Invalid;
    case ProjectAssetTypeFilter::Images:
        return kind == AssetFormat::AssetKind::Texture2D ||
               kind == AssetFormat::AssetKind::Sprite ||
               kind == AssetFormat::AssetKind::Tileset ||
               kind == AssetFormat::AssetKind::EnvironmentMap;
    case ProjectAssetTypeFilter::Models:
        return kind == AssetFormat::AssetKind::StaticMesh ||
               kind == AssetFormat::AssetKind::SkinnedMesh;
    case ProjectAssetTypeFilter::Scenes:
        return kind == AssetFormat::AssetKind::TileMap ||
               kind == AssetFormat::AssetKind::TileMapChunk ||
               kind == AssetFormat::AssetKind::NavigationGrid2D ||
               kind == AssetFormat::AssetKind::Prefab;
    case ProjectAssetTypeFilter::Audio:
        return kind == AssetFormat::AssetKind::AudioClip;
    case ProjectAssetTypeFilter::Animation:
        return kind == AssetFormat::AssetKind::AnimationClip3D ||
               kind == AssetFormat::AssetKind::SpriteAnimationClip;
    case ProjectAssetTypeFilter::Other:
        return kind == AssetFormat::AssetKind::Shader ||
               kind == AssetFormat::AssetKind::Font ||
               kind == AssetFormat::AssetKind::Material ||
               kind == AssetFormat::AssetKind::Fx2D;
    default:
        return false;
    }
}

Core::Result<ProjectAssetBrowserModel> ProjectAssetBrowserModel::Create(
    std::span<const ProjectAssetDescriptor> assets,
    ProjectAssetBrowserConfig config)
{
    if (config.itemCapacity == 0U || config.folderCapacity == 0U ||
        config.folderCapacity > MaxProjectAssetFolderCount ||
        config.itemCapacity > AssetFormat::Wire::MaxManifestEntries ||
        config.dependencyCapacity > AssetFormat::Wire::MaxManifestDependencies ||
        config.dependencyCapacityPerAsset > AssetFormat::Wire::MaxDependenciesPerAsset ||
        config.dependencyCapacityPerAsset > config.dependencyCapacity)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Project asset browser capacities are invalid");
    }
    if (assets.size() > config.itemCapacity)
    {
        return Core::failure(EditorErrorCode::ProjectAssetCapacityExceeded,
                             "Project Catalog exceeds the asset browser capacity");
    }
    try
    {
        std::vector<ProjectAssetDescriptor> ownedAssets(assets.begin(), assets.end());
        std::sort(ownedAssets.begin(), ownedAssets.end(),
                  [](const auto& left, const auto& right) {
                      return left.assetId < right.assetId;
                  });
        if (std::adjacent_find(ownedAssets.begin(), ownedAssets.end(),
                               [](const auto& left, const auto& right) {
                                   return left.assetId == right.assetId;
                               }) != ownedAssets.end())
        {
            return Core::failure(EditorErrorCode::InvalidConfiguration,
                                 "Project asset browser received duplicate AssetIds");
        }

        Core::usize dependencyCount = 0U;
        for (auto& asset : ownedAssets)
        {
            if (!asset.assetId || asset.assetKind == AssetFormat::AssetKind::Invalid ||
                asset.assetTypeVersion == 0U || asset.cookedFileBytes == 0U ||
                asset.cookedFileBytes > AssetFormat::Wire::MaxCookedFileBytes ||
                asset.displayName.empty() || !Core::isStrictUtf8WithoutNul(asset.displayName) ||
                !Core::isStrictUtf8WithoutNul(asset.sourcePathUtf8) ||
                !Core::isStrictUtf8WithoutNul(asset.folderPathUtf8) ||
                asset.dependencyCount != asset.dependencies.size())
            {
                return Core::failure(
                    EditorErrorCode::InvalidConfiguration,
                    "Project asset descriptor has invalid identity, metadata, or display name");
            }
            auto normalizedFolder = normalizeFolderPath(asset.folderPathUtf8);
            if (!normalizedFolder)
            {
                return Core::failure(std::move(normalizedFolder.error()));
            }
            asset.folderPathUtf8 = std::move(*normalizedFolder);
            if (asset.dependencies.size() > config.dependencyCapacityPerAsset ||
                asset.dependencies.size() > config.dependencyCapacity - dependencyCount)
            {
                return Core::failure(EditorErrorCode::ProjectAssetCapacityExceeded,
                                     "Project Catalog exceeds the asset dependency capacity");
            }
            dependencyCount += asset.dependencies.size();

            auto canonicalPath =
                AssetFormat::makeCookedArtifactPath(asset.assetKind, asset.assetId);
            if (!canonicalPath)
            {
                return Core::failure(EditorErrorCode::InvalidConfiguration,
                                     "Project asset descriptor has an unsupported asset kind");
            }
            asset.canonicalRelativeCookedPath.assign(canonicalPath->view());

            std::stable_sort(asset.dependencies.begin(), asset.dependencies.end(),
                             [](const auto& left, const auto& right) {
                                 return left.assetId < right.assetId;
                             });
            std::optional<Core::AssetId> previousDependency;
            for (const auto& dependency : asset.dependencies)
            {
                if (!dependency.assetId ||
                    dependency.expectedKind == AssetFormat::AssetKind::Invalid ||
                    !validDependencyFlags(dependency.flags) ||
                    dependency.assetId == asset.assetId ||
                    (previousDependency && dependency.assetId == *previousDependency))
                {
                    return Core::failure(EditorErrorCode::InvalidConfiguration,
                                         "Project asset descriptor has an invalid dependency");
                }
                previousDependency = dependency.assetId;
            }
        }

        for (const auto& asset : ownedAssets)
        {
            for (const auto& dependency : asset.dependencies)
            {
                const auto target = std::lower_bound(
                    ownedAssets.begin(), ownedAssets.end(), dependency.assetId,
                    [](const auto& candidate, Core::AssetId assetId) {
                        return candidate.assetId < assetId;
                    });
                if (target == ownedAssets.end() || target->assetId != dependency.assetId)
                {
                    return Core::failure(EditorErrorCode::InvalidConfiguration,
                                         "Project asset dependency target is missing");
                }
                if (target->assetKind != dependency.expectedKind)
                {
                    return Core::failure(EditorErrorCode::InvalidConfiguration,
                                         "Project asset dependency kind does not match its target");
                }
            }
        }

        auto folders = buildFolderDescriptors(ownedAssets, {}, config.folderCapacity);
        if (!folders)
        {
            return Core::failure(std::move(folders.error()));
        }

        std::vector<Core::usize> visibleIndices;
        visibleIndices.reserve(ownedAssets.size());
        for (Core::usize index = 0; index < ownedAssets.size(); ++index)
        {
            visibleIndices.push_back(index);
        }
        return ProjectAssetBrowserModel{config, std::move(ownedAssets),
                                        std::move(*folders),
                                        std::move(visibleIndices)};
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Project asset browser allocation failed");
    }
}

ProjectAssetBrowserModel::ProjectAssetBrowserModel(
    ProjectAssetBrowserConfig config,
    std::vector<ProjectAssetDescriptor> assets,
    std::vector<ProjectAssetFolderDescriptor> folders,
    std::vector<Core::usize> visibleIndices) noexcept
    : m_config(config), m_assets(std::move(assets)),
      m_folders(std::move(folders)),
      m_visibleIndices(std::move(visibleIndices))
{
    if (!m_visibleIndices.empty())
    {
        m_selectedVisibleIndex = 0U;
        m_selectedAssetId = m_assets[m_visibleIndices.front()].assetId;
    }
}

const ProjectAssetDescriptor* ProjectAssetBrowserModel::visibleItem(
    Core::usize visibleIndex) const noexcept
{
    if (visibleIndex >= m_visibleIndices.size())
    {
        return nullptr;
    }
    return &m_assets[m_visibleIndices[visibleIndex]];
}

Core::u64 ProjectAssetBrowserModel::visibleItemStableKey(
    Core::usize visibleIndex) const noexcept
{
    if (visibleIndex >= m_visibleIndices.size())
    {
        return 0U;
    }
    return static_cast<Core::u64>(m_visibleIndices[visibleIndex]) + 1U;
}

const ProjectAssetFolderDescriptor* ProjectAssetBrowserModel::folder(
    Core::usize index) const noexcept
{
    return index < m_folders.size() ? &m_folders[index] : nullptr;
}

Core::u64 ProjectAssetBrowserModel::folderStableKey(Core::usize index) const noexcept
{
    return index < m_folders.size() ? static_cast<Core::u64>(index) + 1U : 0U;
}

const ProjectAssetDescriptor* ProjectAssetBrowserModel::selectedItem() const noexcept
{
    return m_selectedVisibleIndex ? visibleItem(*m_selectedVisibleIndex) : nullptr;
}

const ProjectAssetDescriptor* ProjectAssetBrowserModel::inspectorSnapshot(
    Core::AssetId assetId) const noexcept
{
    const auto asset = std::lower_bound(
        m_assets.begin(), m_assets.end(), assetId,
        [](const auto& candidate, Core::AssetId requestedAssetId) {
            return candidate.assetId < requestedAssetId;
        });
    return asset != m_assets.end() && asset->assetId == assetId ? &*asset : nullptr;
}

const ProjectAssetDescriptor* ProjectAssetBrowserModel::spriteAssetForTexture(
    Core::AssetId textureAssetId) const noexcept
{
    if (!textureAssetId) {
        return nullptr;
    }
    for (const ProjectAssetDescriptor& asset : m_assets) {
        if (asset.assetKind != AssetFormat::AssetKind::Sprite) {
            continue;
        }
        for (const AssetFormat::AssetDependency& dependency : asset.dependencies) {
            if (dependency.assetId == textureAssetId &&
                dependency.expectedKind == AssetFormat::AssetKind::Texture2D) {
                return &asset;
            }
        }
    }
    return nullptr;
}

const ProjectAssetDescriptor*
ProjectAssetBrowserModel::selectedInspectorSnapshot() const noexcept
{
    return m_selectedAssetId ? inspectorSnapshot(*m_selectedAssetId) : nullptr;
}

Core::Status ProjectAssetBrowserModel::setTypeFilter(
    ProjectAssetTypeFilter filter) noexcept
{
    if (!validTypeFilter(filter))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Project asset browser type filter is invalid");
    }
    if (m_typeFilter != filter)
    {
        m_typeFilter = filter;
        rebuildVisibleIndices();
    }
    return Core::success();
}

Core::Status ProjectAssetBrowserModel::replaceFolders(
    std::span<const std::string> folderPathsUtf8) noexcept
{
    auto folders = buildFolderDescriptors(m_assets, folderPathsUtf8,
                                          m_config.folderCapacity);
    if (!folders)
    {
        return Core::failure(std::move(folders.error()));
    }
    const bool currentFolderStillExists = std::any_of(
        folders->begin(), folders->end(), [this](const auto& candidate) {
            return candidate.pathUtf8 == m_currentFolderPathUtf8;
        });
    m_folders.swap(*folders);
    if (!currentFolderStillExists)
    {
        m_currentFolderPathUtf8.clear();
    }
    rebuildVisibleIndices();
    return Core::success();
}

Core::Status ProjectAssetBrowserModel::setCurrentFolder(
    std::string_view folderPathUtf8) noexcept
{
    auto normalized = normalizeFolderPath(folderPathUtf8);
    if (!normalized)
    {
        return Core::failure(std::move(normalized.error()));
    }
    const auto folderEntry = std::find_if(
        m_folders.begin(), m_folders.end(), [&normalized](const auto& candidate) {
            return candidate.pathUtf8 == *normalized;
        });
    if (folderEntry == m_folders.end())
    {
        return Core::failure(EditorErrorCode::ProjectAssetNotFound,
                             "Project asset folder is not in the Source snapshot");
    }
    if (m_currentFolderPathUtf8 != *normalized)
    {
        m_currentFolderPathUtf8 = std::move(*normalized);
        rebuildVisibleIndices();
    }
    return Core::success();
}

Core::Status ProjectAssetBrowserModel::setSearchQuery(std::string_view queryUtf8) noexcept
{
    if (!Core::isStrictUtf8WithoutNul(queryUtf8))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Project asset browser search query is not strict UTF-8");
    }
    if (m_searchQueryUtf8 == queryUtf8)
    {
        return Core::success();
    }
    try
    {
        std::string nextQuery(queryUtf8);
        m_searchQueryUtf8.swap(nextQuery);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Project asset browser search query allocation failed");
    }
    rebuildVisibleIndices();
    return Core::success();
}

Core::Status ProjectAssetBrowserModel::selectVisibleIndex(
    Core::usize visibleIndex) noexcept
{
    const auto* asset = visibleItem(visibleIndex);
    if (asset == nullptr)
    {
        return Core::failure(EditorErrorCode::ProjectAssetNotFound,
                             "Project asset browser selection is outside the visible range");
    }
    m_selectedVisibleIndex = visibleIndex;
    m_selectedAssetId = asset->assetId;
    return Core::success();
}

Core::Status ProjectAssetBrowserModel::selectAsset(Core::AssetId assetId) noexcept
{
    for (Core::usize index = 0; index < m_visibleIndices.size(); ++index)
    {
        if (m_assets[m_visibleIndices[index]].assetId == assetId)
        {
            m_selectedVisibleIndex = index;
            m_selectedAssetId = assetId;
            return Core::success();
        }
    }
    return Core::failure(EditorErrorCode::ProjectAssetNotFound,
                         "Project asset is not visible in the current filter");
}

Core::Status ProjectAssetBrowserModel::restoreAssetSelection(Core::AssetId assetId) noexcept
{
    const auto asset = std::lower_bound(
        m_assets.begin(), m_assets.end(), assetId,
        [](const auto& candidate, Core::AssetId requestedAssetId) {
            return candidate.assetId < requestedAssetId;
        });
    if (asset == m_assets.end() || asset->assetId != assetId) {
        return Core::failure(EditorErrorCode::ProjectAssetNotFound,
                             "Project asset browser selection is not in the Catalog snapshot");
    }
    m_selectedAssetId = assetId;
    m_selectedVisibleIndex.reset();
    for (Core::usize index = 0; index < m_visibleIndices.size(); ++index) {
        if (m_assets[m_visibleIndices[index]].assetId == assetId) {
            m_selectedVisibleIndex = index;
            break;
        }
    }
    return Core::success();
}

Core::Status ProjectAssetBrowserModel::renameAsset(
    Core::AssetId assetId, std::string_view displayName) noexcept
{
    if (displayName.empty() || !Core::isStrictUtf8WithoutNul(displayName)) {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Project asset display name must be non-empty strict UTF-8");
    }
    const auto asset = std::lower_bound(
        m_assets.begin(), m_assets.end(), assetId,
        [](const auto& candidate, Core::AssetId requestedAssetId) {
            return candidate.assetId < requestedAssetId;
        });
    if (asset == m_assets.end() || asset->assetId != assetId) {
        return Core::failure(EditorErrorCode::ProjectAssetNotFound,
                             "Project asset is not in the Catalog snapshot");
    }
    try {
        asset->displayName.assign(displayName);
    } catch (const std::bad_alloc&) {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Project asset display name allocation failed");
    }
    rebuildVisibleIndices();
    return Core::success();
}

void ProjectAssetBrowserModel::rebuildVisibleIndices() noexcept
{
    m_visibleIndices.clear();
    m_selectedVisibleIndex.reset();
    for (Core::usize index = 0; index < m_assets.size(); ++index)
    {
        if (!projectAssetMatchesTypeFilter(m_assets[index].assetKind, m_typeFilter) ||
            !pathIsInFolder(m_assets[index].folderPathUtf8,
                            m_currentFolderPathUtf8))
        {
            continue;
        }
        if (!containsAsciiInsensitive(m_assets[index].displayName, m_searchQueryUtf8) &&
            !containsAsciiInsensitive(m_assets[index].sourcePathUtf8, m_searchQueryUtf8) &&
            !containsAsciiInsensitive(m_assets[index].folderPathUtf8, m_searchQueryUtf8) &&
            !containsAsciiInsensitive(projectAssetKindLabel(m_assets[index].assetKind),
                                      m_searchQueryUtf8))
        {
            continue;
        }
        if (m_selectedAssetId && m_assets[index].assetId == *m_selectedAssetId)
        {
            m_selectedVisibleIndex = m_visibleIndices.size();
        }
        m_visibleIndices.push_back(index);
    }
    if (!m_selectedVisibleIndex && !m_selectedAssetId && !m_visibleIndices.empty())
    {
        m_selectedVisibleIndex = 0U;
        if (!m_selectedAssetId)
        {
            m_selectedAssetId = m_assets[m_visibleIndices.front()].assetId;
        }
    }
}

} // namespace Tina::Editor
