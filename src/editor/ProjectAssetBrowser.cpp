#include <tina/editor/ProjectAssetBrowser.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace Tina::Editor {
namespace {

[[nodiscard]] bool validFilter(ProjectAssetFilter filter) noexcept
{
    return filter == ProjectAssetFilter::All || filter == ProjectAssetFilter::TwoD ||
           filter == ProjectAssetFilter::ThreeD || filter == ProjectAssetFilter::Media;
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

bool projectAssetMatchesFilter(AssetFormat::AssetKind kind,
                               ProjectAssetFilter filter) noexcept
{
    if (filter == ProjectAssetFilter::All)
    {
        return kind != AssetFormat::AssetKind::Invalid;
    }
    if (filter == ProjectAssetFilter::TwoD)
    {
        return kind == AssetFormat::AssetKind::Sprite ||
               kind == AssetFormat::AssetKind::Tileset ||
               kind == AssetFormat::AssetKind::TileMap ||
               kind == AssetFormat::AssetKind::SpriteAnimationClip ||
               kind == AssetFormat::AssetKind::TileMapChunk ||
               kind == AssetFormat::AssetKind::NavigationGrid2D ||
               kind == AssetFormat::AssetKind::Fx2D;
    }
    if (filter == ProjectAssetFilter::ThreeD)
    {
        return kind == AssetFormat::AssetKind::StaticMesh ||
               kind == AssetFormat::AssetKind::SkinnedMesh ||
               kind == AssetFormat::AssetKind::AnimationClip3D ||
               kind == AssetFormat::AssetKind::Material ||
               kind == AssetFormat::AssetKind::Prefab ||
               kind == AssetFormat::AssetKind::EnvironmentMap;
    }
    if (filter == ProjectAssetFilter::Media)
    {
        return kind == AssetFormat::AssetKind::Texture2D ||
               kind == AssetFormat::AssetKind::Shader ||
               kind == AssetFormat::AssetKind::Font ||
               kind == AssetFormat::AssetKind::AudioClip;
    }
    return false;
}

Core::Result<ProjectAssetBrowserModel> ProjectAssetBrowserModel::Create(
    std::span<const ProjectAssetDescriptor> assets,
    ProjectAssetBrowserConfig config)
{
    if (config.itemCapacity == 0U ||
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
                asset.dependencyCount != asset.dependencies.size())
            {
                return Core::failure(
                    EditorErrorCode::InvalidConfiguration,
                    "Project asset descriptor has invalid identity, metadata, or display name");
            }
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

        std::vector<Core::usize> visibleIndices;
        visibleIndices.reserve(ownedAssets.size());
        for (Core::usize index = 0; index < ownedAssets.size(); ++index)
        {
            visibleIndices.push_back(index);
        }
        return ProjectAssetBrowserModel{config, std::move(ownedAssets),
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
    std::vector<Core::usize> visibleIndices) noexcept
    : m_config(config), m_assets(std::move(assets)),
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

Core::Status ProjectAssetBrowserModel::setFilter(ProjectAssetFilter filter) noexcept
{
    if (!validFilter(filter))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Project asset browser filter is invalid");
    }
    if (m_filter != filter)
    {
        m_filter = filter;
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

void ProjectAssetBrowserModel::rebuildVisibleIndices() noexcept
{
    m_visibleIndices.clear();
    m_selectedVisibleIndex.reset();
    for (Core::usize index = 0; index < m_assets.size(); ++index)
    {
        if (!projectAssetMatchesFilter(m_assets[index].assetKind, m_filter))
        {
            continue;
        }
        if (!containsAsciiInsensitive(m_assets[index].displayName, m_searchQueryUtf8) &&
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
