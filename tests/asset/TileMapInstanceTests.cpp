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

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

TEST(TileMapInstanceTests, CreateEditChunkRevisionAndSolidQuery)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));

    const std::array tiles{
        AssetFormat::TilesetTileDesc{.localId = 1,
                                     .materialFlags = AssetFormat::TilesetWire::MaterialSolid,
                                     .u0 = 0.0f,
                                     .v0 = 0.0f,
                                     .u1 = 0.5f,
                                     .v1 = 0.5f},
        AssetFormat::TilesetTileDesc{.localId = 2,
                                     .materialFlags = 0,
                                     .u0 = 0.5f,
                                     .v0 = 0.0f,
                                     .u1 = 1.0f,
                                     .v1 = 0.5f},
    };
    auto tilesetBytes = AssetFormat::writeTilesetPayloadBytes(AssetFormat::TilesetPayloadDesc{
        .tilePixelWidth = 16,
        .tilePixelHeight = 16,
        .tiles = tiles,
    });
    ASSERT_TRUE(tilesetBytes.has_value());
    auto tileset = AssetFormat::parseTilesetPayload(*tilesetBytes);
    ASSERT_TRUE(tileset.has_value());

    // 4x2 map: solid floor on y=0
    const std::array<Core::u16, 8> cells{1, 1, 1, 1, 0, 2, 0, 0};
    auto mapBytes = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
        .widthCells = 4,
        .heightCells = 2,
        .cellSizeMeters = 1.0f,
        .tiles = cells,
        .tilesetId = tilesetId,
    });
    ASSERT_TRUE(mapBytes.has_value());
    auto map = AssetFormat::parseTileMapPayload(*mapBytes);
    ASSERT_TRUE(map.has_value());

    auto instance = TileMapInstance::Create(*map, *tileset, mapId, tilesetId,
                                            TileMapInstanceConfig{.chunkSizeCells = 2, .memoryResource = &memory});
    ASSERT_TRUE(instance.has_value()) << instance.error().message;
    EXPECT_EQ(instance->widthCells(), 4U);
    EXPECT_EQ(instance->heightCells(), 2U);
    EXPECT_EQ(instance->chunkCountX(), 2U);
    EXPECT_EQ(instance->chunkCountY(), 1U);
    EXPECT_EQ(instance->tileIdAt(0, 0), 1U);
    EXPECT_EQ(instance->tileIdAt(1, 1), 2U);

    const auto revBefore = instance->chunkRevision(0, 0);
    ASSERT_TRUE(instance->setTile(0, 0, 2).has_value());
    EXPECT_EQ(instance->tileIdAt(0, 0), 2U);
    EXPECT_GT(instance->chunkRevision(0, 0), revBefore);
    // Other chunk unchanged.
    EXPECT_EQ(instance->chunkRevision(1, 0), 1U);

    std::pmr::vector<TileMapSolidHit> hits{&memory};
    // Query entire map: remaining solids at (1,0)(2,0)(3,0) — (0,0) no longer solid
    auto count = instance->querySolidAabb(TileMapSolidQuery{.minX = 0.0f, .minY = 0.0f, .maxX = 4.0f, .maxY = 2.0f},
                                          hits);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 3U);
    EXPECT_EQ(hits.size(), 3U);

    // Unknown tile id rejected.
    auto bad = instance->setTile(0, 0, 99);
    ASSERT_FALSE(bad.has_value());
}

TEST(TileMapInstanceTests, RejectsUnknownCellInSourceMap)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array tiles{AssetFormat::TilesetTileDesc{
        .localId = 1, .materialFlags = AssetFormat::TilesetWire::MaterialSolid, .u0 = 0, .v0 = 0, .u1 = 1, .v1 = 1}};
    auto tilesetBytes = AssetFormat::writeTilesetPayloadBytes(
        AssetFormat::TilesetPayloadDesc{.tilePixelWidth = 8, .tilePixelHeight = 8, .tiles = tiles});
    auto tileset = AssetFormat::parseTilesetPayload(*tilesetBytes);
    ASSERT_TRUE(tileset.has_value());

    const std::array<Core::u16, 1> cells{9};
    auto mapBytes = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
        .widthCells = 1, .heightCells = 1, .cellSizeMeters = 1.0f, .tiles = cells});
    auto map = AssetFormat::parseTileMapPayload(*mapBytes);
    ASSERT_TRUE(map.has_value());

    auto instance = TileMapInstance::Create(
        *map, *tileset, *Core::AssetId::fromBytes(idBytes(1U)), *Core::AssetId::fromBytes(idBytes(2U)),
        TileMapInstanceConfig{.chunkSizeCells = 1, .memoryResource = &memory});
    ASSERT_FALSE(instance.has_value());
}

} // namespace
} // namespace Tina::Asset
