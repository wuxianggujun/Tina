#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include "support/TileMapInstanceTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <vector>

namespace Tina::Asset {
namespace {

inline constexpr AssetFormat::TileMapLayerId TileLayerId = 10;
inline constexpr AssetFormat::TileMapLayerId ObjectLayerId = 20;

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
    const std::array objects{
        AssetFormat::TileMapObjectDesc{
            .stableObjectId = 100,
            .kind = AssetFormat::TileMapObjectKind::Point,
            .name = "spawn",
            .x = 1.0f,
            .y = 1.0f,
        },
    };
    const std::array layers{
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = TileLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = true,
            .name = "visual",
            .cells = cells,
        },
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = ObjectLayerId,
            .kind = AssetFormat::TileMapLayerKind::Object,
            .visible = true,
            .name = "objects",
            .objects = objects,
        },
    };
    auto instance = TestSupport::makeResidentTileMapInstance(4, 2, 2, mapId, tilesetId, *tileset, layers, memory);
    ASSERT_TRUE(instance.has_value()) << instance.error().message;
    EXPECT_EQ(instance->widthCells(), 4U);
    EXPECT_EQ(instance->heightCells(), 2U);
    EXPECT_EQ(instance->chunkCountX(), 2U);
    EXPECT_EQ(instance->chunkCountY(), 1U);
    auto tile00 = instance->tileIdAt(TileLayerId, 0, 0);
    auto tile11 = instance->tileIdAt(TileLayerId, 1, 1);
    ASSERT_TRUE(tile00.has_value());
    ASSERT_TRUE(tile11.has_value());
    EXPECT_EQ(*tile00, 1U);
    EXPECT_EQ(*tile11, 2U);
    auto objectLayer = instance->layer(ObjectLayerId);
    ASSERT_TRUE(objectLayer.has_value());
    EXPECT_EQ(objectLayer->objectCount, 1U);
    const auto spawn = objectLayer->objectAt(0);
    ASSERT_TRUE(spawn.has_value());
    EXPECT_EQ(spawn->name, "spawn");

    const auto revBefore = instance->chunkRevision(TileLayerId, 0, 0);
    ASSERT_TRUE(revBefore.has_value());
    ASSERT_TRUE(instance->setTile(TileLayerId, 0, 0, 2).has_value());
    auto editedTile = instance->tileIdAt(TileLayerId, 0, 0);
    auto editedRevision = instance->chunkRevision(TileLayerId, 0, 0);
    ASSERT_TRUE(editedTile.has_value());
    ASSERT_TRUE(editedRevision.has_value());
    EXPECT_EQ(*editedTile, 2U);
    EXPECT_GT(*editedRevision, *revBefore);
    const auto editedLayerTile = instance->tileIdAt(TileLayerId, 0, 0);
    ASSERT_TRUE(editedLayerTile.has_value());
    EXPECT_EQ(*editedLayerTile, 2U);
    // Other chunk unchanged.
    auto otherRevision = instance->chunkRevision(TileLayerId, 1, 0);
    ASSERT_TRUE(otherRevision.has_value());
    EXPECT_EQ(*otherRevision, 1U);

    std::pmr::vector<TileMapSolidHit> hits{&memory};
    // Query entire map: remaining solids at (1,0)(2,0)(3,0) — (0,0) no longer solid
    auto count = instance->querySolidAabb(
        TileLayerId, TileMapSolidQuery{.minX = 0.0f, .minY = 0.0f, .maxX = 4.0f, .maxY = 2.0f}, hits);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 3U);
    EXPECT_EQ(hits.size(), 3U);

    auto farOutside = instance->querySolidAabb(
        TileLayerId,
        TileMapSolidQuery{.minX = 1.0e30f, .minY = 1.0e30f, .maxX = 2.0e30f, .maxY = 2.0e30f}, hits);
    ASSERT_TRUE(farOutside.has_value());
    EXPECT_EQ(*farOutside, 0U);
    EXPECT_TRUE(hits.empty());

    // Unknown tile id rejected.
    auto bad = instance->setTile(TileLayerId, 0, 0, 99);
    ASSERT_FALSE(bad.has_value());

    auto objectAsTile = instance->tileIdAt(ObjectLayerId, 0, 0);
    ASSERT_FALSE(objectAsTile.has_value());
    EXPECT_EQ(objectAsTile.error().code, AssetErrorCode::TileMapLayerTypeMismatch);
    auto missingLayer = instance->chunkRevision(999, 0, 0);
    ASSERT_FALSE(missingLayer.has_value());
    EXPECT_EQ(missingLayer.error().code, AssetErrorCode::TileMapLayerNotFound);
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
    const std::array layers{
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = TileLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = true,
            .name = "visual",
            .cells = cells,
        },
    };
    auto instance = TestSupport::makeResidentTileMapInstance(1, 1, 1, *Core::AssetId::fromBytes(idBytes(1U)),
                                                              *Core::AssetId::fromBytes(idBytes(2U)), *tileset,
                                                              layers, memory);
    ASSERT_FALSE(instance.has_value());
}

} // namespace
} // namespace Tina::Asset
