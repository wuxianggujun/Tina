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

enum class ProjectAssetFilter : Core::u8 {
    All = 0,
    TwoD = 1,
    ThreeD = 2,
    Media = 3,
};

enum class ProjectAssetOpenKind : Core::u8 {
    AssetInspector = 0,
    World3D = 1,
    TileMap2D = 2,
    SpriteAnimation2D = 3,
};

struct ProjectAssetDescriptor final {
    Core::AssetId assetId{};
    AssetFormat::AssetKind assetKind = AssetFormat::AssetKind::Invalid;
    Core::u16 assetTypeVersion = 0;
    Core::u32 dependencyCount = 0;
    Core::u64 cookedFileBytes = 0;
    std::string displayName{};
    // Source and folder paths are Editor-facing UTF-8 metadata. They are not
    // part of the Runtime Catalog identity and may be empty for generated
    // assets that have no source-import owner.
    std::string sourcePathUtf8{};
    std::string folderPathUtf8{};
    std::string canonicalRelativeCookedPath{};
    std::vector<AssetFormat::AssetDependency> dependencies{};

    friend bool operator==(const ProjectAssetDescriptor&,
                           const ProjectAssetDescriptor&) = default;
};

struct ProjectAssetBrowserConfig final {
    Core::usize itemCapacity = 4096;
    Core::usize dependencyCapacity = AssetFormat::Wire::MaxManifestDependencies;
    Core::usize dependencyCapacityPerAsset = AssetFormat::Wire::MaxDependenciesPerAsset;
};

[[nodiscard]] std::string_view projectAssetKindLabel(
    AssetFormat::AssetKind kind) noexcept;
[[nodiscard]] ProjectAssetOpenKind projectAssetOpenKind(
    AssetFormat::AssetKind kind) noexcept;
[[nodiscard]] bool projectAssetMatchesFilter(
    AssetFormat::AssetKind kind, ProjectAssetFilter filter) noexcept;

// Owning, deterministic project asset index for Editor UI. Input entries are
// copied and sorted by AssetId. visibleItem() and Inspector snapshot views expire
// on destruction only; filter and selection changes do not move owned descriptors.
class ProjectAssetBrowserModel final {
public:
    ProjectAssetBrowserModel(const ProjectAssetBrowserModel&) = delete;
    ProjectAssetBrowserModel& operator=(const ProjectAssetBrowserModel&) = delete;
    ProjectAssetBrowserModel(ProjectAssetBrowserModel&&) noexcept = default;
    ProjectAssetBrowserModel& operator=(ProjectAssetBrowserModel&&) noexcept = default;

    [[nodiscard]] static Core::Result<ProjectAssetBrowserModel>
    Create(std::span<const ProjectAssetDescriptor> assets,
           ProjectAssetBrowserConfig config = {});

    [[nodiscard]] const ProjectAssetBrowserConfig& config() const noexcept
    {
        return m_config;
    }
    [[nodiscard]] Core::usize itemCount() const noexcept { return m_assets.size(); }
    [[nodiscard]] std::span<const ProjectAssetDescriptor> items() const noexcept
    {
        return m_assets;
    }
    [[nodiscard]] Core::usize visibleItemCount() const noexcept
    {
        return m_visibleIndices.size();
    }
    [[nodiscard]] ProjectAssetFilter filter() const noexcept { return m_filter; }
    [[nodiscard]] std::string_view searchQuery() const noexcept
    {
        return m_searchQueryUtf8;
    }
    [[nodiscard]] std::optional<Core::usize> selectedVisibleIndex() const noexcept
    {
        return m_selectedVisibleIndex;
    }
    // Logical selection survives a filter/search that hides its row.
    [[nodiscard]] std::optional<Core::AssetId> selectedAssetId() const noexcept
    {
        return m_selectedAssetId;
    }
    [[nodiscard]] const ProjectAssetDescriptor*
    visibleItem(Core::usize visibleIndex) const noexcept;
    // Stable within one sorted Catalog snapshot. Returns zero for an invalid
    // visible index; valid keys are the owned sorted asset index plus one.
    [[nodiscard]] Core::u64 visibleItemStableKey(Core::usize visibleIndex) const noexcept;
    [[nodiscard]] const ProjectAssetDescriptor* selectedItem() const noexcept;
    [[nodiscard]] const ProjectAssetDescriptor*
    inspectorSnapshot(Core::AssetId assetId) const noexcept;
    [[nodiscard]] const ProjectAssetDescriptor*
    spriteAssetForTexture(Core::AssetId textureAssetId) const noexcept;
    [[nodiscard]] const ProjectAssetDescriptor* selectedInspectorSnapshot() const noexcept;

    [[nodiscard]] Core::Status setFilter(ProjectAssetFilter filter) noexcept;
    [[nodiscard]] Core::Status setSearchQuery(std::string_view queryUtf8) noexcept;
    [[nodiscard]] Core::Status selectVisibleIndex(Core::usize visibleIndex) noexcept;
    [[nodiscard]] Core::Status selectAsset(Core::AssetId assetId) noexcept;
    // Restore a Catalog selection without requiring it to be visible under the
    // current filter/search query.
    [[nodiscard]] Core::Status restoreAssetSelection(Core::AssetId assetId) noexcept;
    [[nodiscard]] Core::Status renameAsset(Core::AssetId assetId,
                                            std::string_view displayName) noexcept;
    [[nodiscard]] Core::Status setAssetFolder(Core::AssetId assetId,
                                               std::string_view folderPathUtf8) noexcept;

private:
    ProjectAssetBrowserModel(ProjectAssetBrowserConfig config,
                             std::vector<ProjectAssetDescriptor> assets,
                             std::vector<Core::usize> visibleIndices) noexcept;

    void rebuildVisibleIndices() noexcept;

    ProjectAssetBrowserConfig m_config{};
    std::vector<ProjectAssetDescriptor> m_assets{};
    std::vector<Core::usize> m_visibleIndices{};
    ProjectAssetFilter m_filter = ProjectAssetFilter::All;
    std::string m_searchQueryUtf8{};
    std::optional<Core::AssetId> m_selectedAssetId{};
    std::optional<Core::usize> m_selectedVisibleIndex{};
};

} // namespace Tina::Editor
