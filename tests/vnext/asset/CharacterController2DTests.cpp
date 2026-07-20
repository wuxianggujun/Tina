#include <tina/asset/CharacterController2D.hpp>
#include <tina/asset/GridCollision.hpp>
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

// 8x4 map: solid floor on y=0 across x, solid wall at x=6 for x=0..3 height of 3.
[[nodiscard]] TileMapInstance makePlatformMap(std::pmr::memory_resource& memory)
{
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
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

    std::array<Core::u16, 32> cells{};
    // y=0 floor
    for (Core::u32 x = 0; x < 8; ++x)
    {
        cells[x] = 1;
    }
    // wall column x=6 for y=1..3
    for (Core::u32 y = 1; y < 4; ++y)
    {
        cells[y * 8 + 6] = 1;
    }

    auto mapBytes = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
        .widthCells = 8,
        .heightCells = 4,
        .cellSizeMeters = 1.0f,
        .tiles = cells,
        .tilesetId = tilesetId,
    });
    auto map = AssetFormat::parseTileMapPayload(*mapBytes);
    auto instance = TileMapInstance::Create(*map, *tileset, mapId, tilesetId,
                                            TileMapInstanceConfig{.chunkSizeCells = 4, .memoryResource = &memory});
    EXPECT_TRUE(instance.has_value()) << instance.error().message;
    return std::move(*instance);
}

TEST(CharacterController2DTests, FallsAndLandsOnFloor)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makePlatformMap(memory);
    TileMapGridCollision grid{map};
    CharacterController2D controller(CharacterController2DConfig{
        .halfWidth = 0.3f,
        .halfHeight = 0.5f,
        .gravity = 40.0f,
        .maxFallSpeed = 50.0f,
        .skin = 0.01f,
    });
    // Start above floor: feet at y≈1.5 → center y = 1.5 + halfHeight
    controller.teleport(1.0f, 2.0f, true);

    std::pmr::vector<TileMapSolidHit> scratch{&memory};
    for (int step = 0; step < 60; ++step)
    {
        ASSERT_TRUE(controller
                        .move(grid, 1.0f / 60.0f, CharacterController2DMoveInput{.wishVelocityX = 0.0f}, scratch)
                        .has_value());
        if (controller.state().grounded)
        {
            break;
        }
    }
    EXPECT_TRUE(controller.state().grounded);
    // Standing on floor y=1 (top of cell y=0): feet at 1 + skin → center ≈ 1.5 + skin
    EXPECT_NEAR(controller.state().positionY, 1.0f + controller.config().halfHeight + controller.config().skin, 0.05f);
    EXPECT_FLOAT_EQ(controller.state().velocityY, 0.0f);
}

TEST(CharacterController2DTests, HorizontalHitsWallAndStops)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makePlatformMap(memory);
    TileMapGridCollision grid{map};
    CharacterController2D controller(CharacterController2DConfig{
        .halfWidth = 0.3f,
        .halfHeight = 0.5f,
        .gravity = 0.0f,
        .maxFallSpeed = 0.0f,
        .skin = 0.01f,
    });
    // On floor, left of wall at x=6
    controller.teleport(4.0f, 1.0f + 0.5f + 0.01f, true);

    std::pmr::vector<TileMapSolidHit> scratch{&memory};
    for (int step = 0; step < 120; ++step)
    {
        ASSERT_TRUE(controller
                        .move(grid, 1.0f / 60.0f, CharacterController2DMoveInput{.wishVelocityX = 8.0f}, scratch)
                        .has_value());
        if (controller.state().hitRight)
        {
            break;
        }
    }
    EXPECT_TRUE(controller.state().hitRight);
    EXPECT_FLOAT_EQ(controller.state().velocityX, 0.0f);
    // Right edge of body should be just left of wall x=6
    EXPECT_LT(controller.state().positionX + controller.config().halfWidth, 6.0f);
    EXPECT_GT(controller.state().positionX + controller.config().halfWidth, 5.5f);
}

TEST(CharacterController2DTests, JumpLeavesGround)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makePlatformMap(memory);
    TileMapGridCollision grid{map};
    CharacterController2D controller(CharacterController2DConfig{
        .halfWidth = 0.3f,
        .halfHeight = 0.5f,
        .gravity = 30.0f,
        .skin = 0.01f,
    });
    controller.teleport(1.0f, 1.0f + 0.5f + 0.01f, true);

    std::pmr::vector<TileMapSolidHit> scratch{&memory};
    // Settle
    ASSERT_TRUE(controller.move(grid, 1.0f / 60.0f, CharacterController2DMoveInput{}, scratch).has_value());
    ASSERT_TRUE(controller.state().grounded);

    ASSERT_TRUE(controller
                    .move(grid, 1.0f / 60.0f,
                          CharacterController2DMoveInput{.wishVelocityX = 0.0f, .jump = true, .jumpSpeed = 10.0f},
                          scratch)
                    .has_value());
    EXPECT_FALSE(controller.state().grounded);
    EXPECT_GT(controller.state().velocityY, 0.0f);
    EXPECT_GT(controller.state().positionY, 1.5f);
}

} // namespace
} // namespace Tina::Asset
