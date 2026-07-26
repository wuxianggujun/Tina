#pragma once

#include <tina/asset/AssetBindingResolver.hpp>
#include <tina/asset/TileChunkView.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/render/RenderScene.hpp>

#include <memory_resource>
#include <vector>

namespace Tina::Asset {

struct TileChunkSpriteEmitParams final {
    // Copyable weak Tileset handle. Emission resolves its required Texture2D
    // binding for this call and retains neither the handle owner nor resolver.
    AssetHandle tileset{};
    AssetBindingResolver bindingResolver{};
    // Base for stableEntityKey generation: base + (cellY * mapWidth + cellX) + 1.
    Core::u64 stableEntityKeyBase = 1;
    Core::i16 sortingLayer = 0;
    Core::i32 orderInLayerBase = 0;
    Core::u8 red = 255;
    Core::u8 green = 255;
    Core::u8 blue = 255;
    Core::u8 alpha = 255;
    // Optional world offset applied to all tile centers (map local → world).
    float originX = 0.0f;
    float originY = 0.0f;
};

// Emits one RenderSprite2DInput per non-empty cell in the chunk.
// Center is cell center in map-local meters (+ optional origin). UV from tileset material table.
// Empty/hidden chunks do not invoke the resolver. A non-empty chunk resolves
// exactly once; missing/zero bindings fail closed with an empty `out`.
// Clears `out` first. Returns number of sprites written.
[[nodiscard]] Core::Result<Core::u32>
emitTileChunkSprites(const TileMapInstance& map, const TileChunkView& chunk, const TileChunkSpriteEmitParams& params,
                     std::pmr::vector<Render::RenderSprite2DInput>& out);

// Convenience: extract visible chunks then emit sprites for each (order: chunk row-major, then cells).
// Resolves the Tileset once for the complete non-empty visible set. Hidden,
// off-camera, or empty results do not invoke the resolver. Clears `out` first.
// Returns total sprites written.
[[nodiscard]] Core::Result<Core::u32>
emitVisibleTileMapSprites(const TileMapInstance& map, AssetFormat::TileMapLayerId layerId,
                          const TileChunkCameraQuery& camera,
                          const TileChunkSpriteEmitParams& params, std::pmr::vector<Render::RenderSprite2DInput>& out);

} // namespace Tina::Asset
