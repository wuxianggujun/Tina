#include <tina/asset/GridCollision.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset/TileMapPhysicsSync.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/physics2d/PhysicsWorld2D.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>
#include <utility>

namespace Tina {
namespace {

inline constexpr AssetFormat::TileMapLayerId CollisionLayerId = 20;

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

TEST(TileMapPhysicsSyncTest, SyncsSolidFloorToStaticBodiesAndContactsDynamic)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(21U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(22U));

    const std::array tiles{
        AssetFormat::TilesetTileDesc{
            .localId = 1,
            .materialFlags = AssetFormat::TilesetWire::MaterialSolid,
            .u0 = 0.0f,
            .v0 = 0.0f,
            .u1 = 1.0f,
            .v1 = 1.0f},
        AssetFormat::TilesetTileDesc{
            .localId = 2,
            .materialFlags = 0,
            .u0 = 0.0f,
            .v0 = 0.0f,
            .u1 = 1.0f,
            .v1 = 1.0f},
    };
    auto tilesetBytes = AssetFormat::writeTilesetPayloadBytes(AssetFormat::TilesetPayloadDesc{
        .tilePixelWidth = 16,
        .tilePixelHeight = 16,
        .tiles = tiles,
    });
    ASSERT_TRUE(tilesetBytes.has_value());
    auto tileset = AssetFormat::parseTilesetPayload(*tilesetBytes);
    ASSERT_TRUE(tileset.has_value());

    // 4x2: solid floor on y=0, non-solid/empty above
    const std::array<Core::u16, 8> cells{1, 1, 1, 1, 0, 2, 0, 0};
    const std::array layers{
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
        .heightCells = 2,
        .cellSizeMeters = 1.0f,
        .layers = layers,
        .tilesetId = tilesetId,
    });
    ASSERT_TRUE(mapBytes.has_value());
    auto map = AssetFormat::parseTileMapPayload(*mapBytes);
    ASSERT_TRUE(map.has_value());

    auto instance = Asset::TileMapInstance::Create(
        *map,
        *tileset,
        mapId,
        tilesetId,
        Asset::TileMapInstanceConfig{.chunkSizeCells = 2, .memoryResource = &memory});
    ASSERT_TRUE(instance.has_value()) << instance.error().message;

    Asset::TileMapGridCollision grid{*instance, CollisionLayerId};
    Physics2D::PhysicsGridSolidCell2D solidScratch[16]{};
    auto collected = Asset::collectAllSolidCellsForPhysics(grid, solidScratch);
    ASSERT_TRUE(collected) << collected.error().message;
    EXPECT_EQ(collected->totalFound, 4U);
    EXPECT_EQ(collected->written, 4U);
    EXPECT_FALSE(collected->overflow);

    Physics2D::PhysicsWorld2DConfig worldConfig;
    worldConfig.bodyCapacity = 16;
    worldConfig.shapeCapacity = 16;
    worldConfig.contactBeginCapacity = 16;
    worldConfig.contactEndCapacity = 16;
    worldConfig.contactHitCapacity = 4;
    worldConfig.commandCapacity = 8;
    worldConfig.solverSubStepCount = 1;
    worldConfig.gravityMetersPerSecondSquared = {0.0F, -9.8F};
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    Physics2D::PhysicsBodyId bodies[8]{};
    Physics2D::PhysicsGridBodySyncConfig2D syncConfig;
    syncConfig.cellSizeMeters = 0.0F; // take from grid
    syncConfig.enableContactEvents = true;
    auto synced = Asset::syncTileMapSolidsToStaticBodies(
        grid,
        world,
        syncConfig,
        bodies,
        solidScratch);
    ASSERT_TRUE(synced) << synced.error().message;
    EXPECT_EQ(synced->totalFound, 4U);
    EXPECT_EQ(synced->written, 4U);
    EXPECT_EQ(world.stats().bodyCount, 4U);

    auto floor = world.bodyState(bodies[0]);
    ASSERT_TRUE(floor) << floor.error().message;
    EXPECT_NEAR(floor->positionMeters.y, 0.5F, 1.0e-4F);

    Physics2D::PhysicsBody2DDesc dynamicBody;
    dynamicBody.type = Physics2D::PhysicsBodyType2D::Dynamic;
    dynamicBody.positionMeters = {1.5F, 4.0F};
    dynamicBody.linearVelocityMetersPerSecond = {0.0F, -15.0F};
    Physics2D::PhysicsShape2DDesc box;
    box.kind = Physics2D::PhysicsShapeKind2D::Box;
    box.halfExtentsMeters = {0.4F, 0.4F};
    box.density = 1.0F;
    box.enableContactEvents = true;
    auto dynamic = world.createBody(dynamicBody);
    ASSERT_TRUE(dynamic) << dynamic.error().message;
    auto dynamicShape = world.createShape(*dynamic, box);
    ASSERT_TRUE(dynamicShape) << dynamicShape.error().message;

    bool sawBegin = false;
    for (int step = 0; step < 180; ++step) {
        ASSERT_TRUE(world.step());
        auto contacts = world.contactEvents();
        ASSERT_TRUE(contacts);
        if (!contacts->beginEvents.empty()) {
            sawBegin = true;
            break;
        }
    }
    EXPECT_TRUE(sawBegin);

    auto after = world.bodyState(*dynamic);
    ASSERT_TRUE(after) << after.error().message;
    EXPECT_LT(after->positionMeters.y, 4.0F);
    EXPECT_GT(after->positionMeters.y, 0.5F);
}

TEST(TileMapPhysicsSyncTest, ScratchOverflowIsReportedAndSyncFails)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array tiles{AssetFormat::TilesetTileDesc{
        .localId = 1,
        .materialFlags = AssetFormat::TilesetWire::MaterialSolid,
        .u0 = 0,
        .v0 = 0,
        .u1 = 1,
        .v1 = 1}};
    auto tilesetBytes = AssetFormat::writeTilesetPayloadBytes(
        AssetFormat::TilesetPayloadDesc{.tilePixelWidth = 8, .tilePixelHeight = 8, .tiles = tiles});
    auto tileset = AssetFormat::parseTilesetPayload(*tilesetBytes);
    ASSERT_TRUE(tileset.has_value());

    const std::array<Core::u16, 4> cells{1, 1, 1, 1};
    const std::array layers{
        AssetFormat::TileMapLayerDesc{
            .stableLayerId = CollisionLayerId,
            .kind = AssetFormat::TileMapLayerKind::Tile,
            .visible = false,
            .name = "collision",
            .tiles = cells,
        },
    };
    auto mapBytes = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
        .widthCells = 2,
        .heightCells = 2,
        .cellSizeMeters = 1.0f,
        .layers = layers});
    auto map = AssetFormat::parseTileMapPayload(*mapBytes);
    ASSERT_TRUE(map.has_value());

    auto instance = Asset::TileMapInstance::Create(
        *map,
        *tileset,
        *Core::AssetId::fromBytes(idBytes(1U)),
        *Core::AssetId::fromBytes(idBytes(2U)),
        Asset::TileMapInstanceConfig{.chunkSizeCells = 2, .memoryResource = &memory});
    ASSERT_TRUE(instance.has_value());

    Asset::TileMapGridCollision grid{*instance, CollisionLayerId};
    Physics2D::PhysicsGridSolidCell2D tiny[1]{};
    auto collected = Asset::collectAllSolidCellsForPhysics(grid, tiny);
    ASSERT_TRUE(collected);
    EXPECT_EQ(collected->totalFound, 4U);
    EXPECT_EQ(collected->written, 1U);
    EXPECT_TRUE(collected->overflow);

    Physics2D::PhysicsWorld2DConfig worldConfig;
    worldConfig.bodyCapacity = 8;
    worldConfig.shapeCapacity = 8;
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig);
    ASSERT_TRUE(worldResult);
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    Physics2D::PhysicsBodyId bodies[4]{};
    auto sync = Asset::syncTileMapSolidsToStaticBodies(
        grid,
        world,
        {},
        bodies,
        tiny);
    EXPECT_FALSE(sync);
    EXPECT_EQ(world.stats().bodyCount, 0U);
}

} // namespace
} // namespace Tina
