#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::SampleTerraria {

// Local tile ids; must match assets/world_assets.recipe `tile` lines.
inline constexpr Core::u16 TileAir = 1;
inline constexpr Core::u16 TileDirt = 2;
inline constexpr Core::u16 TileStone = 3;
inline constexpr Core::u16 TileGrass = 4;

// Atlas is 5x1; the player quad samples the fifth texel directly.
inline constexpr float PlayerAtlasU0 = 0.8F;
inline constexpr float PlayerAtlasU1 = 1.0F;

// Recipe-cooked tilemaps always use 16x16 chunks (RecipeChunkSize in CatalogCook.cpp),
// independent of the `tilemap` line. World dims are chosen as exact multiples so no
// partial chunk exists, which keeps the resident-capacity math exact.
inline constexpr Core::u32 WorldWidthCells = 256;
inline constexpr Core::u32 WorldHeightCells = 128;
inline constexpr Core::u32 RecipeChunkSizeCells = 16;
inline constexpr float WorldCellSizeMeters = 1.0F;

inline constexpr Core::u32 WorldChunksX = WorldWidthCells / RecipeChunkSizeCells;
inline constexpr Core::u32 WorldChunksY = WorldHeightCells / RecipeChunkSizeCells;
// Every chunk stays resident for the whole run. A chunk that gets unloaded and
// re-streamed is restored from its cooked payload, which would silently undo every
// hole the player dug, so the sample never evicts.
inline constexpr Core::u32 WorldChunkCount = WorldChunksX * WorldChunksY;

// Single tile layer drives both rendering and collision: collision reads material
// flags off the tileset, and nothing in the engine interprets layer properties.
inline constexpr Core::u32 WorldTileLayerId = 10;

struct WorldGenConfig final {
    Core::u32 seed = 1337;
    // Mean surface height in cells, measured from the bottom (y = 0).
    float surfaceBaseCells = 78.0F;
    float surfaceAmplitudeCells = 9.0F;
    // Grass band thickness, then dirt, then stone all the way down.
    Core::u32 dirtDepthCells = 12;
    // Caves are carved below this to keep the walkable surface intact.
    Core::u32 caveCeilingMarginCells = 6;
    float caveThreshold = 0.62F;
};

// Deterministic value hash. Not a quality noise function; it only has to be stable
// across runs and platforms so the printed evidence is reproducible.
[[nodiscard]] inline float hashToUnitFloat(Core::u32 x, Core::u32 y, Core::u32 seed) noexcept
{
    Core::u32 h = seed;
    h ^= x * 0x9E3779B9U;
    h = (h ^ (h >> 15U)) * 0x85EBCA6BU;
    h ^= y * 0xC2B2AE35U;
    h = (h ^ (h >> 13U)) * 0xC2B2AE35U;
    h ^= h >> 16U;
    return static_cast<float>(h & 0x00FFFFFFU) / static_cast<float>(0x01000000U);
}

// Smooth-ish 1D value noise over cell columns, built from two hashed lattices.
[[nodiscard]] inline float columnNoise(Core::u32 x, Core::u32 seed, Core::u32 period) noexcept
{
    const Core::u32 cell = x / period;
    const float t = static_cast<float>(x % period) / static_cast<float>(period);
    const float a = hashToUnitFloat(cell, 0U, seed);
    const float b = hashToUnitFloat(cell + 1U, 0U, seed);
    const float smooth = t * t * (3.0F - 2.0F * t);
    return a + (b - a) * smooth;
}

[[nodiscard]] inline Core::u32 surfaceHeightAt(Core::u32 x, const WorldGenConfig& config) noexcept
{
    const float coarse = columnNoise(x, config.seed, 32U) - 0.5F;
    const float fine = columnNoise(x, config.seed ^ 0x5A5A5A5AU, 8U) - 0.5F;
    const float offset = (coarse * 2.0F + fine) * config.surfaceAmplitudeCells;
    float height = config.surfaceBaseCells + offset;
    if (height < 8.0F)
    {
        height = 8.0F;
    }
    const float ceiling = static_cast<float>(WorldHeightCells) - 8.0F;
    if (height > ceiling)
    {
        height = ceiling;
    }
    return static_cast<Core::u32>(height);
}

// Generated cell grid, row 0 == bottom of the world (TileMapInstance uses a
// bottom-left origin, and recipe `row` lines are consumed in order from y = 0).
struct GeneratedWorld final {
    std::vector<Core::u16> cells{};
    Core::u32 widthCells = 0;
    Core::u32 heightCells = 0;
    Core::u32 solidCells = 0;
    Core::u32 airCells = 0;
    Core::u32 spawnCellX = 0;
    Core::u32 spawnCellY = 0;

    [[nodiscard]] Core::u16 at(Core::u32 x, Core::u32 y) const noexcept
    {
        return cells[static_cast<std::size_t>(y) * widthCells + x];
    }
};

[[nodiscard]] inline GeneratedWorld generateWorld(const WorldGenConfig& config)
{
    GeneratedWorld world{};
    world.widthCells = WorldWidthCells;
    world.heightCells = WorldHeightCells;
    world.cells.assign(static_cast<std::size_t>(WorldWidthCells) * WorldHeightCells, TileAir);

    for (Core::u32 x = 0; x < WorldWidthCells; ++x)
    {
        const Core::u32 surface = surfaceHeightAt(x, config);
        for (Core::u32 y = 0; y <= surface && y < WorldHeightCells; ++y)
        {
            Core::u16 tile = TileStone;
            if (y == surface)
            {
                tile = TileGrass;
            }
            else if (surface - y <= config.dirtDepthCells)
            {
                tile = TileDirt;
            }

            // Carve caves only well below the surface so spawn stays walkable.
            if (tile != TileGrass && y + config.caveCeilingMarginCells < surface)
            {
                const float cave = hashToUnitFloat(x, y, config.seed ^ 0xBEEF0001U) * 0.45F +
                                   columnNoise(x * 3U + y, config.seed ^ 0x1234ABCDU, 6U) * 0.55F;
                if (cave > config.caveThreshold)
                {
                    tile = TileAir;
                }
            }

            world.cells[static_cast<std::size_t>(y) * WorldWidthCells + x] = tile;
        }
    }

    world.spawnCellX = WorldWidthCells / 2U;
    const Core::u32 spawnSurface = surfaceHeightAt(world.spawnCellX, config);
    world.spawnCellY = spawnSurface + 2U;

    for (const Core::u16 cell : world.cells)
    {
        if (cell == TileAir)
        {
            ++world.airCells;
        }
        else
        {
            ++world.solidCells;
        }
    }
    return world;
}

// Appends the `tilemap` block for `world` to `recipeText`, which must already contain
// the texture/tileset/tile lines. Every cell is a real tile id (air included), so every
// 16x16 chunk carries a chunk ref and stays editable for the whole run.
inline void appendTileMapRecipe(std::string& recipeText, std::string_view tileMapIdHex,
                                std::string_view tilesetIdHex, const GeneratedWorld& world)
{
    const auto appendU32 = [&recipeText](Core::u32 value) {
        char buffer[16]{};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        recipeText.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
    };

    recipeText.append("tilemap ");
    recipeText.append(tileMapIdHex);
    recipeText.push_back(' ');
    recipeText.append(tilesetIdHex);
    recipeText.push_back(' ');
    appendU32(world.widthCells);
    recipeText.push_back(' ');
    appendU32(world.heightCells);
    recipeText.append(" 1.0\n");

    recipeText.append("tilelayer ");
    appendU32(WorldTileLayerId);
    recipeText.append(" 1 terrain\n");
    recipeText.append("property role terrain\n");

    for (Core::u32 y = 0; y < world.heightCells; ++y)
    {
        recipeText.append("row");
        for (Core::u32 x = 0; x < world.widthCells; ++x)
        {
            recipeText.push_back(' ');
            appendU32(world.at(x, y));
        }
        recipeText.push_back('\n');
    }

    recipeText.append("endlayer\n");
    recipeText.append("endtilemap\n");
}

} // namespace Tina::SampleTerraria
