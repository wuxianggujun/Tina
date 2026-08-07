#include <tina/editor/EditorDocumentTabs.hpp>

#include <tina/core/text/Utf8.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace Tina::Editor {

EditorDocumentKind editorDocumentKindForAsset(AssetFormat::AssetKind kind) noexcept
{
    switch (kind)
    {
    case AssetFormat::AssetKind::Prefab:
        return EditorDocumentKind::World3D;
    case AssetFormat::AssetKind::TileMap:
        return EditorDocumentKind::TileMap2D;
    case AssetFormat::AssetKind::SpriteAnimationClip:
        return EditorDocumentKind::SpriteAnimation2D;
    default:
        return EditorDocumentKind::AssetInspector;
    }
}

EditorDocumentWorkspace editorDocumentWorkspace(EditorDocumentKind kind) noexcept
{
    switch (kind)
    {
    case EditorDocumentKind::World2D:
    case EditorDocumentKind::TileMap2D:
    case EditorDocumentKind::SpriteAnimation2D:
        return EditorDocumentWorkspace::TwoD;
    case EditorDocumentKind::World3D:
        return EditorDocumentWorkspace::ThreeD;
    case EditorDocumentKind::AssetInspector:
    default:
        return EditorDocumentWorkspace::None;
    }
}

Core::Result<EditorDocumentTabs> EditorDocumentTabs::Create(
    std::span<const EditorDocumentTabDesc> initialTabs,
    EditorDocumentTabsConfig config)
{
    if (config.tabCapacity == 0U || config.tabCapacity > 64U ||
        config.titleByteCapacity == 0U || config.titleByteCapacity > 1024U)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor document tab configuration is outside supported limits");
    }
    if (initialTabs.size() > config.tabCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentTabCapacityExceeded,
                             "Initial Editor documents exceed the tab capacity");
    }
    try
    {
        std::vector<EditorDocumentTabDesc> tabs;
        tabs.reserve(config.tabCapacity);
        EditorDocumentTabs result{config, std::move(tabs), 0U};
        for (const auto& initial : initialTabs)
        {
            if (auto status = result.validateTab(initial); !status)
            {
                return Core::failure(std::move(status.error()));
            }
            if (result.find(initial.key))
            {
                return Core::failure(EditorErrorCode::InvalidConfiguration,
                                     "Initial Editor document tabs contain duplicate keys");
            }
            result.m_tabs.push_back(initial);
        }
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor document tab allocation failed");
    }
}

EditorDocumentTabs::EditorDocumentTabs(EditorDocumentTabsConfig config,
                                       std::vector<EditorDocumentTabDesc> tabs,
                                       Core::usize activeIndex) noexcept
    : m_config(config), m_tabs(std::move(tabs)), m_activeIndex(activeIndex)
{
}

const EditorDocumentTabDesc* EditorDocumentTabs::tab(Core::usize index) const noexcept
{
    return index < m_tabs.size() ? &m_tabs[index] : nullptr;
}

const EditorDocumentTabDesc* EditorDocumentTabs::activeTab() const noexcept
{
    return tab(m_activeIndex);
}

std::optional<Core::usize> EditorDocumentTabs::find(EditorDocumentKey key) const noexcept
{
    const auto found = std::find_if(m_tabs.begin(), m_tabs.end(),
                                    [key](const auto& tab) { return tab.key == key; });
    if (found == m_tabs.end())
    {
        return std::nullopt;
    }
    return static_cast<Core::usize>(std::distance(m_tabs.begin(), found));
}

Core::Result<Core::usize> EditorDocumentTabs::open(EditorDocumentTabDesc tab)
{
    if (const auto existing = find(tab.key))
    {
        m_activeIndex = *existing;
        return *existing;
    }
    if (auto status = validateTab(tab); !status)
    {
        return Core::failure(std::move(status.error()));
    }
    if (m_tabs.size() >= m_config.tabCapacity)
    {
        return Core::failure(EditorErrorCode::DocumentTabCapacityExceeded,
                             "Editor document tab capacity is exhausted");
    }
    try
    {
        m_tabs.push_back(std::move(tab));
        m_activeIndex = m_tabs.size() - 1U;
        return m_activeIndex;
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor document tab allocation failed");
    }
}

Core::Status EditorDocumentTabs::activate(Core::usize index) noexcept
{
    if (index >= m_tabs.size())
    {
        return Core::failure(EditorErrorCode::DocumentTabNotFound,
                             "Editor document tab does not exist");
    }
    m_activeIndex = index;
    return Core::success();
}

Core::Status EditorDocumentTabs::close(Core::usize index, bool discardDirty) noexcept
{
    if (index >= m_tabs.size())
    {
        return Core::failure(EditorErrorCode::DocumentTabNotFound,
                             "Editor document tab does not exist");
    }
    if (m_tabs[index].pinned)
    {
        return Core::failure(EditorErrorCode::PinnedDocumentCannotClose,
                             "Pinned Editor document cannot be closed");
    }
    if (m_tabs[index].dirty && !discardDirty)
    {
        return Core::failure(EditorErrorCode::DirtyDocumentRequiresConfirmation,
                             "Modified Editor document requires explicit discard confirmation");
    }
    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_tabs.empty())
    {
        m_activeIndex = 0U;
    }
    else if (m_activeIndex > index)
    {
        --m_activeIndex;
    }
    else if (m_activeIndex >= m_tabs.size())
    {
        m_activeIndex = m_tabs.size() - 1U;
    }
    return Core::success();
}

Core::Status EditorDocumentTabs::setDirty(Core::usize index, bool dirty) noexcept
{
    if (index >= m_tabs.size())
    {
        return Core::failure(EditorErrorCode::DocumentTabNotFound,
                             "Editor document tab does not exist");
    }
    m_tabs[index].dirty = dirty;
    return Core::success();
}

Core::Status EditorDocumentTabs::rename(Core::usize index, std::string title)
{
    if (index >= m_tabs.size())
    {
        return Core::failure(EditorErrorCode::DocumentTabNotFound,
                             "Editor document tab does not exist");
    }
    if (title.empty() || title.size() > m_config.titleByteCapacity ||
        !Core::isStrictUtf8WithoutNul(title))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor document tab title must be bounded strict UTF-8");
    }
    try
    {
        m_tabs[index].title = std::move(title);
        return Core::success();
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Editor document tab title allocation failed");
    }
}

Core::Status EditorDocumentTabs::validateTab(
    const EditorDocumentTabDesc& tab) const noexcept
{
    if (tab.title.empty() || tab.title.size() > m_config.titleByteCapacity ||
        !Core::isStrictUtf8WithoutNul(tab.title))
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Editor document tab title must be bounded strict UTF-8");
    }
    const bool builtInDocument = tab.key.kind == EditorDocumentKind::World2D ||
                                 tab.key.kind == EditorDocumentKind::World3D;
    if (!builtInDocument && !tab.key.assetId)
    {
        return Core::failure(EditorErrorCode::InvalidConfiguration,
                             "Catalog-backed Editor document requires an AssetId");
    }
    return Core::success();
}

} // namespace Tina::Editor
