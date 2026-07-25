#include <tina/asset/CharacterController2D.hpp>
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
    bytes[15] = static_cast<std::byte>(seed ^ 0x3CU);
    return bytes;
}

// 8x4: solid floor y=0; solid wall x=6 for y=1..3 (matches CharacterController unit fixture).
[[nodiscard]] Asset::TileMapInstance makePlatformMap(std::pmr::memory_resource& memory)
{
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(41U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(42U));
    const std::array tiles{AssetFormat::TilesetTileDesc{
        .localId = 1,
        .materialFlags = AssetFormat::TilesetWire::MaterialSolid,
        .u0 = 0,
        .v0 = 0,
        .u1 = 1,
        .v1 = 1,
    }};
    auto tilesetBytes = AssetFormat::writeTilesetPayloadBytes(
        AssetFormat::TilesetPayloadDesc{.tilePixelWidth = 16, .tilePixelHeight = 16, .tiles = tiles});
    auto tileset = AssetFormat::parseTilesetPayload(*tilesetBytes);
    EXPECT_TRUE(tileset.has_value());

    std::array<Core::u16, 32> cells{};
    for (Core::u32 x = 0; x < 8; ++x)
    {
        cells[x] = 1;
    }
    for (Core::u32 y = 1; y < 4; ++y)
    {
        cells[y * 8 + 6] = 1;
    }

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
        .widthCells = 8,
        .heightCells = 4,
        .cellSizeMeters = 1.0f,
        .layers = layers,
        .tilesetId = tilesetId,
    });
    auto map = AssetFormat::parseTileMapPayload(*mapBytes);
    EXPECT_TRUE(map.has_value());
    auto instance = Asset::TileMapInstance::Create(
        *map, *tileset, mapId, tilesetId,
        Asset::TileMapInstanceConfig{.chunkSizeCells = 4, .memoryResource = &memory});
    EXPECT_TRUE(instance.has_value()) << instance.error().message;
    return std::move(*instance);
}

TEST(CharacterControllerPhysicsCoexistenceTest, GridControllerAndDynamicBodyShareTileSolids)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makePlatformMap(memory);
    Asset::TileMapGridCollision grid{map, CollisionLayerId};

    Physics2D::PhysicsWorld2DConfig worldConfig;
    worldConfig.bodyCapacity = 64;
    worldConfig.shapeCapacity = 64;
    worldConfig.contactBeginCapacity = 32;
    worldConfig.contactEndCapacity = 32;
    worldConfig.contactHitCapacity = 8;
    worldConfig.commandCapacity = 16;
    worldConfig.solverSubStepCount = 1;
    worldConfig.gravityMetersPerSecondSquared = {0.0F, -20.0F};
    auto worldResult = Physics2D::PhysicsWorld2D::Create(worldConfig);
    ASSERT_TRUE(worldResult) << worldResult.error().message;
    Physics2D::PhysicsWorld2D world = std::move(*worldResult);

    Physics2D::PhysicsGridSolidCell2D solidScratch[64]{};
    Physics2D::PhysicsBodyId staticBodies[64]{};
    Physics2D::PhysicsGridBodySyncConfig2D syncConfig;
    syncConfig.cellSizeMeters = 0.0F;
    syncConfig.enableContactEvents = true;
    auto synced = Asset::syncTileMapSolidsToStaticBodies(grid, world, syncConfig, staticBodies, solidScratch);
    ASSERT_TRUE(synced) << synced.error().message;
    // Floor 8 + wall column 3 = 11 solid cells
    EXPECT_EQ(synced->totalFound, 11U);
    EXPECT_EQ(synced->written, 11U);
    EXPECT_EQ(world.stats().bodyCount, 11U);

    Asset::CharacterController2D controller(Asset::CharacterController2DConfig{
        .halfWidth = 0.3f,
        .halfHeight = 0.5f,
        .gravity = 40.0f,
        .maxFallSpeed = 50.0f,
        .skin = 0.01f,
    });
    controller.teleport(1.0f, 2.5f, true);

    Physics2D::PhysicsBody2DDesc dynamicDesc;
    dynamicDesc.type = Physics2D::PhysicsBodyType2D::Dynamic;
    dynamicDesc.positionMeters = {3.0F, 3.5F};
    dynamicDesc.linearVelocityMetersPerSecond = {0.0F, 0.0F};
    Physics2D::PhysicsShape2DDesc box;
    box.kind = Physics2D::PhysicsShapeKind2D::Box;
    box.halfExtentsMeters = {0.25F, 0.25F};
    box.density = 1.0F;
    box.enableContactEvents = true;
    auto dynamic = world.createBody(dynamicDesc);
    ASSERT_TRUE(dynamic) << dynamic.error().message;
    auto dynamicShape = world.createShape(*dynamic, box);
    ASSERT_TRUE(dynamicShape) << dynamicShape.error().message;

    std::pmr::vector<Asset::TileMapSolidHit> controllerScratch{&memory};
    bool controllerGrounded = false;
    bool dynamicContacted = false;
    for (int step = 0; step < 180; ++step)
    {
        ASSERT_TRUE(controller
                        .move(grid, 1.0f / 60.0f, Asset::CharacterController2DMoveInput{.wishVelocityX = 0.0f},
                              controllerScratch)
                        .has_value());
        if (controller.state().grounded)
        {
            controllerGrounded = true;
        }

        ASSERT_TRUE(world.step()) << "physics step failed";
        auto contacts = world.contactEvents();
        ASSERT_TRUE(contacts);
        for (const auto& begin : contacts->beginEvents)
        {
            if (begin.bodyA == *dynamic || begin.bodyB == *dynamic)
            {
                dynamicContacted = true;
            }
        }
        if (controllerGrounded && dynamicContacted)
        {
            break;
        }
    }

    EXPECT_TRUE(controllerGrounded);
    EXPECT_NEAR(controller.state().positionY,
                1.0f + controller.config().halfHeight + controller.config().skin, 0.08f);
    EXPECT_FLOAT_EQ(controller.state().velocityY, 0.0f);

    EXPECT_TRUE(dynamicContacted);
    auto after = world.bodyState(*dynamic);
    ASSERT_TRUE(after) << after.error().message;
    EXPECT_LT(after->positionMeters.y, 3.5F);
    EXPECT_GT(after->positionMeters.y, 0.5F);

    // Same wall solid exists for controller walk-stop while physics statics remain live.
    controller.teleport(4.0f, 1.0f + 0.5f + 0.01f, true);
    controller.state().velocityX = 0.0f;
    controller.state().velocityY = 0.0f;
    bool hitWall = false;
    for (int step = 0; step < 120; ++step)
    {
        ASSERT_TRUE(controller
                        .move(grid, 1.0f / 60.0f, Asset::CharacterController2DMoveInput{.wishVelocityX = 8.0f},
                              controllerScratch)
                        .has_value());
        ASSERT_TRUE(world.step());
        if (controller.state().hitRight)
        {
            hitWall = true;
            break;
        }
    }
    EXPECT_TRUE(hitWall);
    EXPECT_LT(controller.state().positionX + controller.config().halfWidth, 6.0f);
    EXPECT_EQ(world.stats().bodyCount, 12U); // 11 static + 1 dynamic
}

} // namespace
} // namespace Tina
