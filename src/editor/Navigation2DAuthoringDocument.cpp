#include <tina/editor/Navigation2DAuthoringDocument.hpp>

#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/CatalogPackage.hpp>
#include <tina/asset/CatalogSnapshot.hpp>
#include <tina/asset/TileMapNavigation2D.hpp>
#include <tina/asset_format/NavigationGrid2DPayload.hpp>
#include <tina/editor/EditorErrors.hpp>

#include <limits>
#include <new>
#include <utility>

namespace Tina::Editor {

Core::Status Navigation2DAuthoringDocument::bakeFromTileMap(
    const Asset::TileMapInstance& map,
    const Asset::TileMapNavigation2DDataBuildConfig& config,
    Core::AssetId navigationAssetId,
    Core::u64 tileMapRevision,
    AssetFormat::TargetPlatform platform)
{
    if (!navigationAssetId || tileMapRevision == 0U)
    {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Navigation bake requires an AssetId and non-zero TileMap revision");
    }
    auto built = Asset::buildTileMapNavigation2DData(map, config);
    if (!built)
    {
        return Core::failure(std::move(built.error()));
    }
    const auto& data = built->data;
    const AssetFormat::NavigationGrid2DPayloadDesc desc{
        .widthCells = data.widthCells(),
        .heightCells = data.heightCells(),
        .originXMeters = data.originXMeters(),
        .originYMeters = data.originYMeters(),
        .cellSizeMeters = data.cellSizeMeters(),
        .cellFlags = data.cellFlags(),
        .traversalCosts = data.traversalCosts(),
    };
    auto payload = AssetFormat::writeNavigationGrid2DPayloadBytes(desc);
    if (!payload)
    {
        return Core::failure(std::move(payload.error()));
    }
    auto cooked = AssetFormat::writeCookedNavigationGrid2DAsset(
        navigationAssetId, desc, platform);
    if (!cooked)
    {
        return Core::failure(std::move(cooked.error()));
    }
    auto path = AssetFormat::makeCookedArtifactPath(
        AssetFormat::AssetKind::NavigationGrid2D, navigationAssetId);
    if (!path)
    {
        return Core::failure(std::move(path.error()));
    }
    try
    {
        Navigation2DBakePreview candidate{
            .assetId = navigationAssetId,
            .sourceTileMapRevision = tileMapRevision,
            .targetPlatform = platform,
            .path = *path,
            .payloadBytes = std::move(*payload),
            .cookedBytes = std::move(*cooked),
        };
        m_preview = std::move(candidate);
        m_catalogPublished = false;
        if (m_revision != (std::numeric_limits<Core::u64>::max)())
        {
            ++m_revision;
        }
        return Core::success();
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Navigation bake publication allocation failed");
    }
}

Core::Result<Asset::CatalogSnapshot> Navigation2DAuthoringDocument::stageCatalog(
    std::string_view stagingRootUtf8,
    std::string_view baselineRootUtf8,
    const Asset::CatalogSnapshot& baseline,
    Asset::CatalogPackageStageConfig config) const
{
    if (!hasBake())
    {
        return Core::failure(EditorErrorCode::InvalidAuthoringOperation,
                             "Navigation Catalog publication requires a successful bake");
    }
    try
    {
        std::vector<Core::AssetId> cleanAssetIds;
        cleanAssetIds.reserve(baseline.entryCount());
        for (Core::u32 index = 0; index < baseline.entryCount(); ++index)
        {
            const auto entry = baseline.entry(index);
            if (!entry)
            {
                return Core::failure(Core::CoreErrorCode::Internal,
                                     "Navigation Catalog baseline entry disappeared");
            }
            if (entry->assetId != m_preview.assetId)
            {
                cleanAssetIds.push_back(entry->assetId);
            }
        }
        Asset::CatalogCookRequest dirtyRequest{
            .targetPlatform = m_preview.targetPlatform,
        };
        dirtyRequest.assets.push_back(Asset::CatalogCookAssetSpec{
            .assetKind = AssetFormat::AssetKind::NavigationGrid2D,
            .assetId = m_preview.assetId,
            .assetTypeVersion = AssetFormat::NavigationGrid2DWire::SchemaVersion,
            .payload = m_preview.payloadBytes,
        });
        return Asset::cookAndStageIncrementalCatalogPackage(
            stagingRootUtf8, baselineRootUtf8, baseline, cleanAssetIds,
            dirtyRequest, config);
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory,
                             "Navigation Catalog staging allocation failed");
    }
}

} // namespace Tina::Editor
