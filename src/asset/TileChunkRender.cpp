#include <tina/asset/TileChunkRender.hpp>

#include <tina/asset/AssetErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::u64 makeStableEntityKey(Core::u64 base, Core::u32 cellX, Core::u32 cellY,
                                            Core::u32 mapWidth) noexcept
{
    const Core::u64 linear = static_cast<Core::u64>(cellY) * mapWidth + cellX;
    return base + linear + 1U;
}

[[nodiscard]] Core::Result<bool> hasRenderableTiles(const TileMapInstance& map, const TileChunkView& chunk)
{
    if (!map)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile map instance is empty");
    }
    if (chunk.layerId == 0)
    {
        return Core::failure(AssetErrorCode::TileMapLayerNotFound, "tile chunk has no selected layer");
    }
    auto layer = map.layer(chunk.layerId);
    if (!layer)
    {
        return Core::failure(std::move(layer.error()));
    }
    if (layer->kind != AssetFormat::TileMapLayerKind::Tile)
    {
        return Core::failure(AssetErrorCode::TileMapLayerTypeMismatch, "tile chunk render requires tile layer");
    }
    return layer->visible && !chunk.empty && chunk.widthCells != 0 && chunk.heightCells != 0;
}

[[nodiscard]] Core::Result<Render::FrameResourceRef>
resolveTilesetResource(const TileChunkSpriteEmitParams& params, Render::FrameResourceSink& frameResources) noexcept
{
    if (!params.tileset)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "tile chunk render requires a weak Tileset handle");
    }
    if (!params.bindingResolver)
    {
        return Core::failure(AssetErrorCode::SpriteBindingNotFound,
                             "tile chunk render requires a frame resource resolver");
    }
    auto resource = params.bindingResolver(params.tileset, frameResources);
    if (!resource)
    {
        return Core::failure(std::move(resource.error()).withContext("TileChunkRender", "resolveTileset"));
    }
    if (!*resource)
    {
        return Core::failure(AssetErrorCode::SpriteBindingNotFound,
                             "Tileset asset has no live Sprite2D texture binding");
    }
    return *resource;
}

[[nodiscard]] Core::Result<Core::u32> emitTileChunkSpritesWithResource(
    const TileMapInstance& map,
    const TileChunkView& chunk,
    const TileChunkSpriteEmitParams& params,
    const Render::Sprite2DProjection& projection,
    const Render::Sprite2DQuad& tileQuad,
    Render::FrameResourceRef texture,
    std::pmr::vector<Render::RenderSprite2DInput>& out)
{
    const Core::usize firstWritten = out.size();
    const float cell = map.cellSizeMeters();
    // One lookup for the whole chunk. Per-cell tileInfoAt() redid the layer scan, the
    // chunk-ref binary search and the resident-chunk scan for each of up to 4096 cells
    // -- all three constant across a chunk.
    auto cells = map.chunkCells(chunk.layerId, chunk.coord);
    if (!cells)
    {
        out.resize(firstWritten);
        return Core::failure(std::move(cells.error()));
    }
    try
    {
        out.reserve(firstWritten + chunk.nonEmptyTileCount);
        for (Core::u32 y = 0; y < chunk.heightCells; ++y)
        {
            for (Core::u32 x = 0; x < chunk.widthCells; ++x)
            {
                const auto info = map.tileInfoForLocalId(cells->localTileIdAt(x, y));
                if (!info.has_value())
                {
                    continue;
                }
                const Core::u32 cellX = chunk.originCellX + x;
                const Core::u32 cellY = chunk.originCellY + y;
                const Render::IsometricGridPoint2D position{
                    params.originX + (static_cast<float>(cellX) + 0.5F) * cell,
                    params.originY + (static_cast<float>(cellY) + 0.5F) * cell,
                    params.elevation,
                };
                const auto center = projection.projectPoint(position);
                auto quad = tileQuad;
                quad.centerX = center.x;
                quad.centerY = center.y;
                if (!quad.isValid())
                {
                    out.resize(firstWritten);
                    return Core::failure(AssetErrorCode::InvalidCatalogConfig,
                                         "tile projection produced invalid quad bounds");
                }
                out.push_back(Render::RenderSprite2DInput{
                    .texture = texture,
                    .stableEntityKey = makeStableEntityKey(params.stableEntityKeyBase, cellX, cellY, map.widthCells()),
                    .quad = quad,
                    .u0 = info->u0,
                    .v0 = info->v0,
                    .u1 = info->u1,
                    .v1 = info->v1,
                    .sortingLayer = params.sortingLayer,
                    .sortDepth = projection.sortDepth(position),
                    .orderInLayer = params.orderInLayerBase,
                    .red = params.red,
                    .green = params.green,
                    .blue = params.blue,
                    .alpha = params.alpha,
                    .flipX = false,
                    .flipY = false,
                    .visible = true,
                });
            }
        }
    } catch (const std::bad_alloc&)
    {
        out.resize(firstWritten);
        return Core::failure(AssetErrorCode::AllocationFailed, "tile chunk sprite emit allocation failed");
    }
    return static_cast<Core::u32>(out.size() - firstWritten);
}

[[nodiscard]] Core::Result<Render::Sprite2DQuad> makeTileQuad(
    const TileMapInstance& map, const TileChunkSpriteEmitParams& params,
    const Render::Sprite2DProjection& projection)
{
    if (!projection.isValid() || !std::isfinite(params.originX) || !std::isfinite(params.originY)
        || !std::isfinite(params.elevation))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile projection or map origin is invalid");
    }
    const Core::u64 cells = static_cast<Core::u64>(map.widthCells()) * map.heightCells();
    if (params.stableEntityKeyBase > (std::numeric_limits<Core::u64>::max)() - cells)
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile stable entity key range overflows");
    }
    const float halfCell = map.cellSizeMeters() * 0.5F;
    const auto quad = projection.ground(Render::Sprite2DQuad{
        .halfAxisXX = halfCell, .halfAxisYY = halfCell,
    });
    if (!quad.isValid())
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile projection has degenerate axes");
    }
    return quad;
}

} // namespace

Core::Result<TileChunkCameraQuery> makeTileChunkCameraQuery(
    const Render::RenderCamera2D& camera, Render::IsometricGridPoint2D mapOrigin) noexcept
{
    if (!std::isfinite(camera.centerX) || !std::isfinite(camera.centerY)
        || !std::isfinite(camera.rotationRadians) || !std::isfinite(camera.worldWidth)
        || !std::isfinite(camera.worldHeight) || !(camera.worldWidth > 0.0F) || !(camera.worldHeight > 0.0F)
        || !std::isfinite(mapOrigin.x) || !std::isfinite(mapOrigin.y) || !std::isfinite(mapOrigin.elevation)
        || (camera.isometricProjection && (!camera.isometricProjection->isValid()
            || std::abs(camera.rotationRadians) > 1.0e-5F)))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile camera projection or map origin is invalid");
    }
    const double cosine = std::cos(static_cast<double>(camera.rotationRadians));
    const double sine = std::sin(static_cast<double>(camera.rotationRadians));
    double minimumX = (std::numeric_limits<double>::max)();
    double minimumY = minimumX;
    double maximumX = -minimumX;
    double maximumY = maximumX;
    for (int y : {-1, 1})
    {
        for (int x : {-1, 1})
        {
            const double localX = x * static_cast<double>(camera.worldWidth) * 0.5;
            const double localY = y * static_cast<double>(camera.worldHeight) * 0.5;
            const double renderX = camera.centerX + cosine * localX - sine * localY;
            const double renderY = camera.centerY + sine * localX + cosine * localY;
            double worldX = renderX;
            double worldY = renderY;
            if (camera.isometricProjection) {
                // Keep inverse bounds in double until the final outward-rounded
                // query. Rounding each corner to float can exclude an edge chunk.
                const auto& basis = *camera.isometricProjection;
                const double gridX = renderX / (static_cast<double>(basis.tileWidthMeters) * 0.5);
                const double gridY = (renderY - static_cast<double>(mapOrigin.elevation) * basis.elevationStepMeters)
                    / (static_cast<double>(basis.tileHeightMeters) * 0.5);
                worldX = (gridX + gridY) * 0.5;
                worldY = (gridY - gridX) * 0.5;
            }
            if (!std::isfinite(worldX) || !std::isfinite(worldY))
            {
                return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile camera inverse projection overflowed");
            }
            minimumX = (std::min)(minimumX, worldX - mapOrigin.x);
            minimumY = (std::min)(minimumY, worldY - mapOrigin.y);
            maximumX = (std::max)(maximumX, worldX - mapOrigin.x);
            maximumY = (std::max)(maximumY, worldY - mapOrigin.y);
        }
    }
    TileChunkCameraQuery query{
        .centerX = static_cast<float>((minimumX + maximumX) * 0.5),
        .centerY = static_cast<float>((minimumY + maximumY) * 0.5),
    };
    query.halfWidth = std::nextafter(static_cast<float>((std::max)(
        maximumX - query.centerX, static_cast<double>(query.centerX) - minimumX)),
        (std::numeric_limits<float>::infinity)());
    query.halfHeight = std::nextafter(static_cast<float>((std::max)(
        maximumY - query.centerY, static_cast<double>(query.centerY) - minimumY)),
        (std::numeric_limits<float>::infinity)());
    if (!std::isfinite(query.centerX) || !std::isfinite(query.centerY) || !std::isfinite(query.halfWidth)
        || !std::isfinite(query.halfHeight) || !(query.halfWidth > 0.0F) || !(query.halfHeight > 0.0F))
    {
        return Core::failure(AssetErrorCode::InvalidCatalogConfig, "tile camera bounds exceed finite grid coordinates");
    }
    return query;
}

Core::Result<Core::u32> emitTileChunkSprites(const TileMapInstance& map, const TileChunkView& chunk,
                                             const TileChunkSpriteEmitParams& params,
                                             const Render::Sprite2DProjection& projection,
                                             Render::FrameResourceSink& frameResources,
                                             std::pmr::vector<Render::RenderSprite2DInput>& out)
{
    out.clear();
    auto renderable = hasRenderableTiles(map, chunk);
    if (!renderable)
    {
        return Core::failure(std::move(renderable.error()));
    }
    if (!*renderable)
    {
        return Core::u32{0};
    }
    auto quad = makeTileQuad(map, params, projection);
    if (!quad) return Core::failure(std::move(quad.error()));
    auto texture = resolveTilesetResource(params, frameResources);
    if (!texture)
    {
        return Core::failure(std::move(texture.error()));
    }
    return emitTileChunkSpritesWithResource(map, chunk, params, projection, *quad, *texture, out);
}

Core::Result<Core::u32> emitVisibleTileMapSprites(const TileMapInstance& map, AssetFormat::TileMapLayerId layerId,
                                                  const Render::RenderCamera2D& camera, const TileChunkSpriteEmitParams& params,
                                                  Render::FrameResourceSink& frameResources,
                                                  TileMapSpriteScratch& scratch)
{
    auto& out = scratch.sprites;
    auto& chunks = scratch.chunks;
    out.clear();
    chunks.clear();
    auto query = makeTileChunkCameraQuery(camera, {params.originX, params.originY, params.elevation});
    if (!query) return Core::failure(std::move(query.error()));
    auto extracted = extractVisibleTileChunks(map, layerId, *query, chunks);
    if (!extracted)
    {
        return Core::failure(std::move(extracted.error()));
    }
    if (chunks.empty())
    {
        return Core::u32{0};
    }
    const Render::Sprite2DProjection projection{camera.isometricProjection};
    auto quad = makeTileQuad(map, params, projection);
    if (!quad) return Core::failure(std::move(quad.error()));
    auto texture = resolveTilesetResource(params, frameResources);
    if (!texture)
    {
        return Core::failure(std::move(texture.error()));
    }

    Core::u32 total = 0;
    try
    {
        // Reserved from counts the instance already tracks, and each chunk appends
        // straight into `out`. The previous shape built every sprite into a scratch
        // vector and then copied it across, so each ~100-byte sprite was written
        // twice per frame and `out` reallocated its way up.
        Core::u32 expected = 0;
        for (const TileChunkView& chunk : chunks)
        {
            expected += chunk.nonEmptyTileCount;
        }
        out.reserve(expected);
        for (const TileChunkView& chunk : chunks)
        {
            auto n = emitTileChunkSpritesWithResource(map, chunk, params, projection, *quad, *texture, out);
            if (!n)
            {
                out.clear();
                return Core::failure(std::move(n.error()));
            }
            total += *n;
        }
    } catch (const std::bad_alloc&)
    {
        out.clear();
        return Core::failure(AssetErrorCode::AllocationFailed, "visible tile map sprite emit allocation failed");
    }
    return total;
}

} // namespace Tina::Asset
