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
               kind == AssetFormat::AssetKind::TileMapChunk;
    }
    if (filter == ProjectAssetFilter::ThreeD)
    {
        return kind == AssetFormat::AssetKind::StaticMesh ||
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
    if (config.itemCapacity == 0U)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Project asset browser capacity must be non-zero");
    }
    if (assets.size() > config.itemCapacity)
    {
        return Core::failure(EditorErrorCode::ProjectAssetCapacityExceeded,
                             "Project Catalog exceeds the asset browser capacity");
    }
    for (const auto& asset : assets)
    {
        if (!asset.assetId || asset.assetKind == AssetFormat::AssetKind::Invalid ||
            asset.displayName.empty() || !Core::isStrictUtf8WithoutNul(asset.displayName))
        {
            return Core::failure(EditorErrorCode::InvalidConfiguration,
                                 "Project asset descriptor has invalid identity, kind, or display name");
        }
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

const ProjectAssetDescriptor* ProjectAssetBrowserModel::selectedItem() const noexcept
{
    return m_selectedVisibleIndex ? visibleItem(*m_selectedVisibleIndex) : nullptr;
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
        if (m_selectedAssetId && m_assets[index].assetId == *m_selectedAssetId)
        {
            m_selectedVisibleIndex = m_visibleIndices.size();
        }
        m_visibleIndices.push_back(index);
    }
    if (!m_selectedVisibleIndex && !m_visibleIndices.empty())
    {
        m_selectedVisibleIndex = 0U;
        m_selectedAssetId = m_assets[m_visibleIndices.front()].assetId;
    }
    if (m_visibleIndices.empty())
    {
        m_selectedAssetId.reset();
    }
}

} // namespace Tina::Editor
