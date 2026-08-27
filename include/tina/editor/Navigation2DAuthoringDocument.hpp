#pragma once

#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/TileMapNavigation2D.hpp>
#include <tina/asset_format/AssetFormat.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/core/id/AssetId.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace Tina::Editor {

struct Navigation2DBakePreview final {
    Core::AssetId assetId{};
    Core::u64 sourceTileMapRevision = 0;
    AssetFormat::TargetPlatform targetPlatform = AssetFormat::TargetPlatform::WindowsX64;
    AssetFormat::CookedArtifactPath path{};
    std::vector<std::byte> payloadBytes{};
    std::vector<std::byte> cookedBytes{};
};

// Owns the last successful NavigationGrid2D bake. Failed bakes are atomic and
// leave the previous preview intact. A TileMap revision mismatch is always
// reported as dirty; callers cannot silently consume a stale bake.
class Navigation2DAuthoringDocument final {
public:
    Navigation2DAuthoringDocument() = default;

    [[nodiscard]] bool hasBake() const noexcept { return m_preview.assetId.hasValue(); }
    [[nodiscard]] Core::u64 revision() const noexcept { return m_revision; }
    [[nodiscard]] Core::u64 sourceTileMapRevision() const noexcept
    {
        return m_preview.sourceTileMapRevision;
    }
    [[nodiscard]] bool isDirtyFor(Core::u64 tileMapRevision) const noexcept
    {
        return !hasBake() || !m_catalogPublished || tileMapRevision == 0U ||
               m_preview.sourceTileMapRevision != tileMapRevision;
    }
    void markCatalogPublished() noexcept { m_catalogPublished = hasBake(); }
    void markCatalogDirty() noexcept { m_catalogPublished = false; }
    [[nodiscard]] const Navigation2DBakePreview& preview() const noexcept
    {
        return m_preview;
    }

    [[nodiscard]] Core::Status bakeFromTileMap(
        const Asset::TileMapInstance& map,
        const Asset::TileMapNavigation2DDataBuildConfig& config,
        Core::AssetId navigationAssetId,
        Core::u64 tileMapRevision,
        AssetFormat::TargetPlatform platform = AssetFormat::TargetPlatform::WindowsX64);

    // Produces a fully validated fresh Catalog stage containing the baked asset
    // plus every unchanged baseline object. The caller owns the returned stage
    // and decides when to atomically switch its live Catalog root.
    [[nodiscard]] Core::Result<Asset::CatalogSnapshot> stageCatalog(
        std::string_view stagingRootUtf8,
        std::string_view baselineRootUtf8,
        const Asset::CatalogSnapshot& baseline,
        Asset::CatalogPackageStageConfig config) const;

private:
    Navigation2DBakePreview m_preview{};
    Core::u64 m_revision = 0;
    bool m_catalogPublished = false;
};

} // namespace Tina::Editor
