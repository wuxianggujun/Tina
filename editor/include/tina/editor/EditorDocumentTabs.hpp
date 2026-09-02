#pragma once

#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Editor {

enum class EditorDocumentKind : Core::u8 {
    World2D = 0,
    World3D = 1,
    TileMap2D = 2,
    SpriteAnimation2D = 3,
    AssetInspector = 4,
};

enum class EditorDocumentWorkspace : Core::u8 {
    None = 0,
    TwoD = 1,
    ThreeD = 2,
};

struct EditorDocumentKey final {
    EditorDocumentKind kind = EditorDocumentKind::World2D;
    // Built-in World2D/World3D documents use an empty AssetId. Catalog-backed
    // documents use their stable Catalog identity.
    Core::AssetId assetId{};

    friend bool operator==(const EditorDocumentKey&, const EditorDocumentKey&) = default;
};

struct EditorDocumentTabDesc final {
    EditorDocumentKey key{};
    std::string title{};
    bool pinned = false;
    bool dirty = false;
};

struct EditorDocumentTabsConfig final {
    Core::usize tabCapacity = 16;
    Core::usize titleByteCapacity = 96;
};

[[nodiscard]] EditorDocumentKind editorDocumentKindForAsset(
    AssetFormat::AssetKind kind) noexcept;
[[nodiscard]] EditorDocumentWorkspace editorDocumentWorkspace(
    EditorDocumentKind kind) noexcept;

// Fixed-capacity owning tab state. open() deduplicates by stable key and
// activates the existing tab. Failed open/activate/close operations preserve
// the current tab list and active selection.
class EditorDocumentTabs final {
public:
    EditorDocumentTabs(const EditorDocumentTabs&) = delete;
    EditorDocumentTabs& operator=(const EditorDocumentTabs&) = delete;
    EditorDocumentTabs(EditorDocumentTabs&&) noexcept = default;
    EditorDocumentTabs& operator=(EditorDocumentTabs&&) noexcept = default;

    [[nodiscard]] static Core::Result<EditorDocumentTabs>
    Create(std::span<const EditorDocumentTabDesc> initialTabs,
           EditorDocumentTabsConfig config = {});

    [[nodiscard]] const EditorDocumentTabsConfig& config() const noexcept
    {
        return m_config;
    }
    [[nodiscard]] Core::usize tabCount() const noexcept { return m_tabs.size(); }
    [[nodiscard]] Core::usize activeIndex() const noexcept { return m_activeIndex; }
    [[nodiscard]] const EditorDocumentTabDesc* tab(Core::usize index) const noexcept;
    [[nodiscard]] const EditorDocumentTabDesc* activeTab() const noexcept;
    [[nodiscard]] std::optional<Core::usize> find(EditorDocumentKey key) const noexcept;

    [[nodiscard]] Core::Result<Core::usize> open(EditorDocumentTabDesc tab);
    [[nodiscard]] Core::Status activate(Core::usize index) noexcept;
    [[nodiscard]] Core::Status close(Core::usize index, bool discardDirty = false) noexcept;
    [[nodiscard]] Core::Status setDirty(Core::usize index, bool dirty) noexcept;
    [[nodiscard]] Core::Status rename(Core::usize index, std::string title);

private:
    EditorDocumentTabs(EditorDocumentTabsConfig config,
                       std::vector<EditorDocumentTabDesc> tabs,
                       Core::usize activeIndex) noexcept;

    [[nodiscard]] Core::Status validateTab(const EditorDocumentTabDesc& tab) const noexcept;

    EditorDocumentTabsConfig m_config{};
    std::vector<EditorDocumentTabDesc> m_tabs{};
    Core::usize m_activeIndex = 0;
};

} // namespace Tina::Editor
