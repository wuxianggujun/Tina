#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/TileMapNavigation2D.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/navigation2d/NavigationErrors.hpp>
#include <tina/navigation2d/NavigationPathfinder2D.hpp>

#include "asset/support/TileMapInstanceTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <utility>
#include <vector>

namespace Tina::Asset {
namespace {

inline constexpr AssetFormat::TileMapLayerId CollisionLayerId = 20;
inline constexpr AssetFormat::TileMapLayerId GameplayLayerId = 30;

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

struct TileMapFixture final {
    Core::AssetId mapId = *Core::AssetId::fromBytes(idBytes(1));
    Core::AssetId tilesetId = *Core::AssetId::fromBytes(idBytes(2));
    std::vector<std::byte> tilesetBytes;
    AssetFormat::TilesetPayloadView tileset{};

    TileMapFixture()
    {
        const std::array tiles{
            AssetFormat::TilesetTileDesc{
                .localId = 1,
                .materialFlags = AssetFormat::TilesetWire::MaterialSolid,
                .u0 = 0.0F,
                .v0 = 0.0F,
                .u1 = 0.5F,
                .v1 = 1.0F,
            },
            AssetFormat::TilesetTileDesc{
                .localId = 2,
                .materialFlags = 0,
                .u0 = 0.5F,
                .v0 = 0.0F,
                .u1 = 1.0F,
                .v1 = 1.0F,
            },
        };
        auto bytes = AssetFormat::writeTilesetPayloadBytes(
            AssetFormat::TilesetPayloadDesc{.tilePixelWidth = 16, .tilePixelHeight = 16, .tiles = tiles});
        EXPECT_TRUE(bytes.has_value()) << (bytes ? "" : bytes.error().message);
        tilesetBytes = std::move(*bytes);
        auto parsed = AssetFormat::parseTilesetPayload(tilesetBytes);
        EXPECT_TRUE(parsed.has_value()) << (parsed ? "" : parsed.error().message);
        tileset = *parsed;
    }
};

TEST(TileMapNavigation2DTests, BuildsSchemaV1DataFromSolidTilesAndTaggedRectangles)
{
    std::pmr::unsynchronized_pool_resource memory;
    TileMapFixture fixture;
    std::array<Core::u16, 15> cells{};
    cells[0] = 1;
    cells[14] = 2; // Non-solid authored tile remains walkable.

    const std::array blockerProperty{
        AssetFormat::TileMapPropertyDesc{.key = "navigation", .value = "blocked"},
    };
    const std::array nonBlockerProperty{
        AssetFormat::TileMapPropertyDesc{.key = "navigation", .value = "walkable"},
    };
    const std::array objects{
        AssetFormat::TileMapObjectDesc{
            .stableObjectId = 101,
            .kind = AssetFormat::TileMapObjectKind::Rectangle,
            .visible = true,
            .name = "crate",
            .x = 1.25F,
            .y = 1.0F,
            .width = 1.0F,
            .height = 1.0F,
            .properties = blockerProperty,
        },
        AssetFormat::TileMapObjectDesc{
            .stableObjectId = 102,
            .kind = AssetFormat::TileMapObjectKind::Rectangle,
            .visible = false,
            .name = "hidden",
            .x = 3.0F,
            .y = 1.0F,
            .width = 1.0F,
            .height = 1.0F,
            .properties = blockerProperty,
        },
        AssetFormat::TileMapObjectDesc{
            .stableObjectId = 103,
            .kind = AssetFormat::TileMapObjectKind::Rectangle,
            .visible = true,
            .name = "zone",
            .x = 4.0F,
            .y = 1.0F,
            .width = 1.0F,
            .height = 1.0F,
            .properties = nonBlockerProperty,
        },
    };
    const std::array layers{
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .cells = cells,
        },
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = GameplayLayerId,
            .kind = AssetFormat::TileMapLayerKind::Object,
            .visible = true,
            .name = "gameplay",
            .objects = objects,
        },
    };
    auto map = TestSupport::makeResidentTileMapInstance(
        5, 3, 4, fixture.mapId, fixture.tilesetId, fixture.tileset, layers, memory);
    ASSERT_TRUE(map.has_value()) << map.error().message;

    auto built = buildTileMapNavigation2DData(
        *map,
        TileMapNavigation2DDataBuildConfig{
            .solidTileLayerId = CollisionLayerId,
            .blockerObjectLayerId = GameplayLayerId,
        },
        memory);
    ASSERT_TRUE(built.has_value()) << built.error().message;
    EXPECT_EQ(built->data.schemaVersion(), Navigation2D::NavigationGrid2DSchema::Version);
    EXPECT_EQ(built->stats.solidTileCells, 1U);
    EXPECT_EQ(built->stats.blockerRectangles, 1U);
    EXPECT_EQ(built->stats.blockedCells, 3U);
    EXPECT_TRUE(built->data.blockedAt({0, 0}));
    EXPECT_TRUE(built->data.blockedAt({1, 1}));
    EXPECT_TRUE(built->data.blockedAt({2, 1}));
    EXPECT_FALSE(built->data.blockedAt({3, 1}));
    EXPECT_FALSE(built->data.blockedAt({4, 2}));

    auto grid = Navigation2D::NavigationGrid2D::Create(
        std::move(built->data), Navigation2D::NavigationGrid2DConfig{.dynamicBlockerCapacity = 2}, memory);
    ASSERT_TRUE(grid.has_value());
    auto pathfinder = Navigation2D::NavigationPathfinder2D::Create({.cellCapacity = 15}, memory);
    ASSERT_TRUE(pathfinder.has_value());
    auto path = pathfinder->findPath(*grid, {0, 2}, {4, 2});
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->state, Navigation2D::NavigationPathQueryState::Reached);
}

TEST(TileMapNavigation2DTests, RejectsWrongLayerKindsAndTaggedPointBlockers)
{
    std::pmr::unsynchronized_pool_resource memory;
    TileMapFixture fixture;
    const std::array<Core::u16, 4> cells{};
    const std::array blockerProperty{
        AssetFormat::TileMapPropertyDesc{.key = "navigation", .value = "blocked"},
    };
    const std::array objects{
        AssetFormat::TileMapObjectDesc{
            .stableObjectId = 201,
            .kind = AssetFormat::TileMapObjectKind::Point,
            .visible = true,
            .name = "invalid_blocker",
            .x = 1.0F,
            .y = 1.0F,
            .properties = blockerProperty,
        },
    };
    const std::array layers{
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .cells = cells,
        },
        TestSupport::TestTileMapLayerDesc{
            .stableLayerId = GameplayLayerId,
            .kind = AssetFormat::TileMapLayerKind::Object,
            .visible = true,
            .objects = objects,
        },
    };
    auto map = TestSupport::makeResidentTileMapInstance(
        2, 2, 2, fixture.mapId, fixture.tilesetId, fixture.tileset, layers, memory);
    ASSERT_TRUE(map.has_value());

    auto wrongSolidLayer = buildTileMapNavigation2DData(
        *map, TileMapNavigation2DDataBuildConfig{.solidTileLayerId = GameplayLayerId}, memory);
    ASSERT_FALSE(wrongSolidLayer.has_value());
    EXPECT_EQ(wrongSolidLayer.error().code, AssetErrorCode::TileMapLayerTypeMismatch);

    auto taggedPoint = buildTileMapNavigation2DData(
        *map,
        TileMapNavigation2DDataBuildConfig{
            .solidTileLayerId = CollisionLayerId,
            .blockerObjectLayerId = GameplayLayerId,
        },
        memory);
    ASSERT_FALSE(taggedPoint.has_value());
    EXPECT_EQ(taggedPoint.error().code, Navigation2D::NavigationErrorCode::InvalidData);
}

TEST(TileMapNavigation2DTests, ReferencedNonResidentChunkFailsWithoutPartialData)
{
    std::pmr::unsynchronized_pool_resource memory;
    TileMapFixture fixture;
    const auto chunkId = *Core::AssetId::fromBytes(idBytes(9));
    const std::array refs{
        AssetFormat::TileMapChunkRefDesc{
            .chunkX = 0,
            .chunkY = 0,
            .widthCells = 1,
            .heightCells = 1,
            .nonEmptyCount = 1,
            .chunkAssetId = chunkId,
        },
    };
    const std::array layers{
        AssetFormat::TileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .chunkRefs = refs,
        },
    };
    auto mapBytes = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
        .widthCells = 1,
        .heightCells = 1,
        .chunkSizeCells = 1,
        .layers = layers,
        .tilesetId = fixture.tilesetId,
    });
    ASSERT_TRUE(mapBytes.has_value());
    auto mapView = AssetFormat::parseTileMapPayload(*mapBytes);
    ASSERT_TRUE(mapView.has_value());
    auto map = TileMapInstance::Create(
        *mapView, fixture.tileset, fixture.mapId, fixture.tilesetId,
        TileMapInstanceConfig{.residentChunkCapacity = 1, .memoryResource = &memory});
    ASSERT_TRUE(map.has_value());

    auto built = buildTileMapNavigation2DData(
        *map, TileMapNavigation2DDataBuildConfig{.solidTileLayerId = CollisionLayerId}, memory);
    ASSERT_FALSE(built.has_value());
    EXPECT_EQ(built.error().code, AssetErrorCode::TileMapChunkNotResident);
}

} // namespace
} // namespace Tina::Asset
