#include <tina/asset/GridCollision.hpp>
#include <tina/asset/TileChunkView.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Asset {
namespace {

inline constexpr AssetFormat::TileMapLayerId VisualLayerId = 10;
inline constexpr AssetFormat::TileMapLayerId CollisionLayerId = 20;

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

[[nodiscard]] TileMapInstance makeMap(std::pmr::memory_resource& memory)
{
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const std::array tiles{
        AssetFormat::TilesetTileDesc{.localId = 1,
                                     .materialFlags = AssetFormat::TilesetWire::MaterialSolid,
                                     .u0 = 0,
                                     .v0 = 0,
                                     .u1 = 1,
                                     .v1 = 1},
        AssetFormat::TilesetTileDesc{.localId = 2, .materialFlags = 0, .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1},
    };
    auto tilesetBytes = AssetFormat::writeTilesetPayloadBytes(
        AssetFormat::TilesetPayloadDesc{.tilePixelWidth = 16, .tilePixelHeight = 16, .tiles = tiles});
    auto tileset = AssetFormat::parseTilesetPayload(*tilesetBytes);
    // 4x4 with only bottom-left 2x2 solid filled -> chunk(0,0) non-empty, others empty
    const std::array<Core::u16, 16> cells{1, 1, 0, 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::array layers{
        AssetFormat::TileMapLayerDesc{
            .stableLayerId = VisualLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = true,
            .name = "visual",
            .tiles = cells,
        },
        AssetFormat::TileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .tiles = cells,
        },
    };
    auto mapBytes = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
        .widthCells = 4,
        .heightCells = 4,
        .cellSizeMeters = 1.0f,
        .layers = layers,
        .tilesetId = tilesetId,
    });
    auto map = AssetFormat::parseTileMapPayload(*mapBytes);
    auto instance = TileMapInstance::Create(*map, *tileset, mapId, tilesetId,
                                            TileMapInstanceConfig{.chunkSizeCells = 2, .memoryResource = &memory});
    EXPECT_TRUE(instance.has_value());
    return std::move(*instance);
}

TEST(TileChunkExtractionTests, VisibleChunksSkipEmptyAndRespectCamera)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeMap(memory);
    std::pmr::vector<TileChunkView> chunks{&memory};

    // Camera covering whole map.
    auto count = extractVisibleTileChunks(
        map, VisualLayerId,
        TileChunkCameraQuery{.centerX = 2.0f, .centerY = 2.0f, .halfWidth = 3.0f, .halfHeight = 3.0f}, chunks);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 1U);
    EXPECT_EQ(chunks.size(), 1U);
    EXPECT_EQ(chunks[0].coord.chunkX, 0U);
    EXPECT_EQ(chunks[0].coord.chunkY, 0U);
    EXPECT_EQ(chunks[0].nonEmptyTileCount, 4U);
    EXPECT_FALSE(chunks[0].empty);

    // Camera only on empty right side.
    chunks.clear();
    count = extractVisibleTileChunks(
        map, VisualLayerId,
        TileChunkCameraQuery{.centerX = 3.5f, .centerY = 3.5f, .halfWidth = 0.4f, .halfHeight = 0.4f}, chunks);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 0U);
}

TEST(TileChunkExtractionTests, CollectChunkCellsAndGridCollision)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeMap(memory);
    std::pmr::vector<TileChunkCell> cells{&memory};
    auto n = collectChunkNonEmptyCells(map, VisualLayerId, TileMapChunkCoord{.chunkX = 0, .chunkY = 0}, cells);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(*n, 4U);
    EXPECT_EQ(cells.size(), 4U);

    std::pmr::vector<TileChunkView> hiddenChunks{&memory};
    auto hidden = extractVisibleTileChunks(
        map, CollisionLayerId,
        TileChunkCameraQuery{.centerX = 2.0f, .centerY = 2.0f, .halfWidth = 3.0f, .halfHeight = 3.0f}, hiddenChunks);
    ASSERT_TRUE(hidden.has_value());
    EXPECT_EQ(*hidden, 0U);
    EXPECT_TRUE(hiddenChunks.empty());

    TileMapGridCollision grid{map, CollisionLayerId};
    EXPECT_EQ(grid.materialFlagsAt(0, 0), AssetFormat::TilesetWire::MaterialSolid);
    EXPECT_EQ(grid.materialFlagsAt(1, 1), 0); // tile 2 non-solid
    EXPECT_EQ(grid.materialFlagsAt(3, 3), 0);

    std::pmr::vector<TileMapSolidHit> hits{&memory};
    auto solids = grid.querySolidAabb(TileMapSolidQuery{.minX = 0.0f, .minY = 0.0f, .maxX = 4.0f, .maxY = 4.0f}, hits);
    ASSERT_TRUE(solids.has_value());
    EXPECT_EQ(*solids, 3U); // three solid tiles (localId 1)
}

} // namespace
} // namespace Tina::Asset
