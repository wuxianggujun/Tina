#pragma once

// Block atlas built in memory: one row of four 16x16 RGBA8 tiles.
//
// Generated rather than cooked. A cooked asset would drag in a recipe file, a temp
// catalog directory and the cooker just to carry 4 KB of pixels that the sample can
// describe in a few lines. Uploaded straight through createTexture2D with Point
// filtering, which is what a blocky texture wants.

#include "ChunkMesh.hpp"

#include <tina/core/base/Types.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace VoxelSample {

// Tile order matches atlasTileFor(): grass, dirt, stone, planks.
[[nodiscard]] inline std::vector<std::byte> makeBlockAtlasRgba8()
{
    struct TileColor final {
        u8 red, green, blue;
    };
    constexpr std::array<TileColor, AtlasTileCount> BaseColors{{
        {90, 158, 58},   // grass
        {139, 109, 63},  // dirt
        {127, 127, 127}, // stone
        {160, 128, 95},  // planks
    }};

    std::vector<std::byte> pixels(static_cast<usize>(AtlasWidth) * AtlasHeight * 4U);
    for (u32 y = 0; y < AtlasHeight; ++y)
    {
        for (u32 x = 0; x < AtlasWidth; ++x)
        {
            const u32 tile = x / AtlasTileSize;
            const u32 tileX = x % AtlasTileSize;
            const TileColor base = BaseColors[tile];

            // Per-pixel jitter so a flat colour does not read as untextured. Reusing
            // the terrain hash keeps the atlas reproducible across runs.
            const u32 noise = hashCoord(static_cast<i32>(x), static_cast<i32>(y), 0x5EEDU);
            i32 shade = static_cast<i32>(noise % 21U) - 10;

            // Planks get horizontal grain and a staggered vertical seam so the
            // player-placed block is visually distinct from the terrain ones.
            if (tile == 3)
            {
                if (y % 5U == 0U)
                {
                    shade -= 28;
                }
                const u32 seamOffset = (y / 5U) % 2U == 0U ? 3U : 11U;
                if (tileX == seamOffset)
                {
                    shade -= 20;
                }
            }
            // Grass gets a darker speckle to break up the top faces.
            else if (tile == 0 && noise % 7U == 0U)
            {
                shade -= 18;
            }

            const auto channel = [shade](u8 value) noexcept {
                const i32 result = static_cast<i32>(value) + shade;
                return static_cast<std::byte>(
                    static_cast<u8>(result < 0 ? 0 : (result > 255 ? 255 : result)));
            };

            const usize offset = (static_cast<usize>(y) * AtlasWidth + x) * 4U;
            pixels[offset] = channel(base.red);
            pixels[offset + 1U] = channel(base.green);
            pixels[offset + 2U] = channel(base.blue);
            pixels[offset + 3U] = static_cast<std::byte>(255U);
        }
    }
    return pixels;
}

} // namespace VoxelSample
