#include <tina/asset/TileChunkRender.hpp>

#include <tina/asset/AssetErrors.hpp>

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

[[nodiscard]] Core::Result<Core::u32> resolveTilesetBinding(const TileChunkSpriteEmitParams& params) noexcept
{
    if (!params.tileset)
    {
        return Core::failure(AssetErrorCode::InvalidHandle, "tile chunk render requires a weak Tileset handle");
    }
    const Core::u32 spriteKey = params.bindingResolver(params.tileset);
    if (spriteKey == 0)
    {
        return Core::failure(AssetErrorCode::SpriteBindingNotFound,
                             "Tileset asset has no live Sprite2D texture binding");
    }
    return spriteKey;
}

[[nodiscard]] Core::Result<Core::u32> emitTileChunkSpritesWithBinding(
    const TileMapInstance& map,
    const TileChunkView& chunk,
    const TileChunkSpriteEmitParams& params,
    Core::u32 spriteKey,
    std::pmr::vector<Render::RenderSprite2DInput>& out)
{
    auto renderable = hasRenderableTiles(map, chunk);
    if (!renderable)
    {
        out.clear();
        return Core::failure(std::move(renderable.error()));
    }
    if (!*renderable)
    {
        out.clear();
        return Core::u32{0};
    }

    out.clear();
    const float cell = map.cellSizeMeters();
    try
    {
        out.reserve(chunk.nonEmptyTileCount);
        Core::i32 order = params.orderInLayerBase;
        for (Core::u32 y = 0; y < chunk.heightCells; ++y)
        {
            for (Core::u32 x = 0; x < chunk.widthCells; ++x)
            {
                const Core::u32 cellX = chunk.originCellX + x;
                const Core::u32 cellY = chunk.originCellY + y;
                auto info = map.tileInfoAt(chunk.layerId, cellX, cellY);
                if (!info)
                {
                    out.clear();
                    return Core::failure(std::move(info.error()));
                }
                if (!*info || (*info)->empty)
                {
                    continue;
                }
                const float centerX = params.originX + (static_cast<float>(cellX) + 0.5f) * cell;
                const float centerY = params.originY + (static_cast<float>(cellY) + 0.5f) * cell;
                out.push_back(Render::RenderSprite2DInput{
                    .spriteKey = spriteKey,
                    .stableEntityKey = makeStableEntityKey(params.stableEntityKeyBase, cellX, cellY, map.widthCells()),
                    .centerX = centerX,
                    .centerY = centerY,
                    .rotationRadians = 0.0f,
                    .widthMeters = cell,
                    .heightMeters = cell,
                    .scaleX = 1.0f,
                    .scaleY = 1.0f,
                    .u0 = (*info)->u0,
                    .v0 = (*info)->v0,
                    .u1 = (*info)->u1,
                    .v1 = (*info)->v1,
                    .sortingLayer = params.sortingLayer,
                    .orderInLayer = order++,
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
        out.clear();
        return Core::failure(AssetErrorCode::AllocationFailed, "tile chunk sprite emit allocation failed");
    }
    return static_cast<Core::u32>(out.size());
}

} // namespace

Core::Result<Core::u32> emitTileChunkSprites(const TileMapInstance& map, const TileChunkView& chunk,
                                             const TileChunkSpriteEmitParams& params,
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
    auto spriteKey = resolveTilesetBinding(params);
    if (!spriteKey)
    {
        return Core::failure(std::move(spriteKey.error()));
    }
    return emitTileChunkSpritesWithBinding(map, chunk, params, *spriteKey, out);
}

Core::Result<Core::u32> emitVisibleTileMapSprites(const TileMapInstance& map, AssetFormat::TileMapLayerId layerId,
                                                  const TileChunkCameraQuery& camera, const TileChunkSpriteEmitParams& params,
                                                  std::pmr::vector<Render::RenderSprite2DInput>& out)
{
    out.clear();
    std::pmr::vector<TileChunkView> chunks{out.get_allocator()};
    auto extracted = extractVisibleTileChunks(map, layerId, camera, chunks);
    if (!extracted)
    {
        return Core::failure(std::move(extracted.error()));
    }
    if (chunks.empty())
    {
        return Core::u32{0};
    }
    auto spriteKey = resolveTilesetBinding(params);
    if (!spriteKey)
    {
        return Core::failure(std::move(spriteKey.error()));
    }

    std::pmr::vector<Render::RenderSprite2DInput> chunkSprites{out.get_allocator()};
    Core::u32 total = 0;
    try
    {
        for (const auto& chunk : chunks)
        {
            auto n = emitTileChunkSpritesWithBinding(map, chunk, params, *spriteKey, chunkSprites);
            if (!n)
            {
                out.clear();
                return Core::failure(std::move(n.error()));
            }
            out.insert(out.end(), chunkSprites.begin(), chunkSprites.end());
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
