#pragma once

#include <tina/asset/AssetFrameResourceResolver.hpp>
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
    AssetFrameResourceResolver bindingResolver{};
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
    float elevation = 0.0F;
};

// Frame-to-frame scratch owned by the caller, never by the packet. Reusing both
// vectors avoids rebuilding/freeing chunk and sprite allocations for every layer.
struct TileMapSpriteScratch final {
    explicit TileMapSpriteScratch(std::pmr::memory_resource& memory = *std::pmr::get_default_resource())
        : chunks(&memory), sprites(&memory) {}

    std::pmr::vector<TileChunkView> chunks;
    std::pmr::vector<Render::RenderSprite2DInput> sprites;
};

// Inverse-project the actual render viewport at the map's elevation before
// subtracting its world origin. The result is a conservative map-local AABB.
[[nodiscard]] Core::Result<TileChunkCameraQuery>
makeTileChunkCameraQuery(const Render::RenderCamera2D& camera,
                        Render::IsometricGridPoint2D mapOrigin = {}) noexcept;

// Emits one RenderSprite2DInput per non-empty cell in the chunk.
// Ground quad corners and depth use the same projection as Scene sprites/FX.
// Empty/hidden chunks do not invoke the resolver. A non-empty chunk resolves
// exactly once; missing/zero bindings fail closed with an empty `out`.
// Clears `out` first. Returns number of sprites written.
[[nodiscard]] Core::Result<Core::u32>
emitTileChunkSprites(const TileMapInstance& map, const TileChunkView& chunk, const TileChunkSpriteEmitParams& params,
                     const Render::Sprite2DProjection& projection,
                     Render::FrameResourceSink& frameResources,
                     std::pmr::vector<Render::RenderSprite2DInput>& out);

// Convenience: extract visible chunks then emit sprites for each (order: chunk row-major, then cells).
// Resolves the Tileset once for the complete non-empty visible set. Hidden,
// off-camera, or empty results do not invoke the resolver. Clears scratch contents
// but keeps capacity; sprites remain borrowed until the next call using scratch.
// Returns total sprites written.
[[nodiscard]] Core::Result<Core::u32>
emitVisibleTileMapSprites(const TileMapInstance& map, AssetFormat::TileMapLayerId layerId,
                          const Render::RenderCamera2D& camera,
                          const TileChunkSpriteEmitParams& params, Render::FrameResourceSink& frameResources,
                          TileMapSpriteScratch& scratch);

} // namespace Tina::Asset
