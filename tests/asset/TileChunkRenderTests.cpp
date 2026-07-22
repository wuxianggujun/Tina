#include <tina/asset/TileChunkRender.hpp>
#include <tina/asset/TileMapInstance.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/render/RenderScene.hpp>

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

[[nodiscard]] TileMapInstance makeMap(std::pmr::memory_resource& memory)
{
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
    auto tilesetBytes = AssetFormat::writeTilesetPayloadBytes(
        AssetFormat::TilesetPayloadDesc{.tilePixelWidth = 16, .tilePixelHeight = 16, .tiles = tiles});
    auto tileset = AssetFormat::parseTilesetPayload(*tilesetBytes);
    // 2x2 all filled
    const std::array<Core::u16, 4> cells{1, 2, 2, 1};
    auto mapBytes = AssetFormat::writeTileMapPayloadBytes(AssetFormat::TileMapPayloadDesc{
        .widthCells = 2,
        .heightCells = 2,
        .cellSizeMeters = 1.0f,
        .tiles = cells,
        .tilesetId = tilesetId,
    });
    auto map = AssetFormat::parseTileMapPayload(*mapBytes);
    auto instance = TileMapInstance::Create(*map, *tileset, mapId, tilesetId,
                                            TileMapInstanceConfig{.chunkSizeCells = 2, .memoryResource = &memory});
    EXPECT_TRUE(instance.has_value()) << instance.error().message;
    return std::move(*instance);
}

TEST(TileChunkRenderTests, EmitSpritesWithUvAndCenter)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeMap(memory);
    std::pmr::vector<TileChunkView> chunks{&memory};
    ASSERT_TRUE(extractVisibleTileChunks(
                    map, TileChunkCameraQuery{.centerX = 1.0f, .centerY = 1.0f, .halfWidth = 2.0f, .halfHeight = 2.0f},
                    chunks)
                    .has_value());
    ASSERT_EQ(chunks.size(), 1U);

    std::pmr::vector<Render::RenderSprite2DInput> sprites{&memory};
    auto n = emitTileChunkSprites(map, chunks[0], TileChunkSpriteEmitParams{.spriteKey = 1}, sprites);
    ASSERT_TRUE(n.has_value()) << n.error().message;
    EXPECT_EQ(*n, 4U);
    EXPECT_EQ(sprites.size(), 4U);

    // Cell (0,0) tile 1 UV 0..0.5, center (0.5, 0.5)
    EXPECT_FLOAT_EQ(sprites[0].centerX, 0.5f);
    EXPECT_FLOAT_EQ(sprites[0].centerY, 0.5f);
    EXPECT_FLOAT_EQ(sprites[0].widthMeters, 1.0f);
    EXPECT_FLOAT_EQ(sprites[0].u0, 0.0f);
    EXPECT_FLOAT_EQ(sprites[0].u1, 0.5f);
    EXPECT_EQ(sprites[0].spriteKey, 1U);

    // Commit into RenderScene
    auto builder = Render::RenderSceneBuilder::Create(Render::RenderSceneCapacity{.spriteCapacity = 16}, memory);
    ASSERT_TRUE(builder.has_value());
    ASSERT_TRUE(builder->beginFrame({}).has_value());
    auto writer = builder->writer();
    ASSERT_TRUE(writer.setCamera2D(Render::RenderCamera2DInput{
                                       .stableCameraKey = 1,
                                       .centerX = 1.0f,
                                       .centerY = 1.0f,
                                       .worldWidth = 4.0f,
                                       .worldHeight = 4.0f,
                                       .actualPixelsPerMeter = 16.0f,
                                   })
                    .has_value());
    for (const auto& sprite : sprites)
    {
        ASSERT_TRUE(writer.addSprite2D(sprite).has_value());
    }
    auto view = builder->commit();
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->sprites2D().size(), 4U);
    EXPECT_FLOAT_EQ(view->sprites2D()[0].u1, 0.5f);
}

TEST(TileChunkRenderTests, EmitVisibleSkipsOffCamera)
{
    std::pmr::unsynchronized_pool_resource memory;
    auto map = makeMap(memory);
    std::pmr::vector<Render::RenderSprite2DInput> sprites{&memory};
    // Camera far away → 0 sprites
    auto n = emitVisibleTileMapSprites(
        map, TileChunkCameraQuery{.centerX = 100.0f, .centerY = 100.0f, .halfWidth = 0.5f, .halfHeight = 0.5f},
        TileChunkSpriteEmitParams{.spriteKey = 7}, sprites);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(*n, 0U);
    EXPECT_TRUE(sprites.empty());
}

} // namespace
} // namespace Tina::Asset
