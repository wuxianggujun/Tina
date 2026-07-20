#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/TileMapPayload.hpp>
#include <tina/asset_format/TilesetPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

TEST(TilesetPayloadTests, WriteParseAndCookedRoundTrip)
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const std::array tiles{
        TilesetTileDesc{.localId = 1, .materialFlags = TilesetWire::MaterialSolid, .u0 = 0.0f, .v0 = 0.0f, .u1 = 0.5f,
                        .v1 = 0.5f},
        TilesetTileDesc{.localId = 2, .materialFlags = 0, .u0 = 0.5f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 0.5f},
    };
    auto payload = writeTilesetPayloadBytes(TilesetPayloadDesc{
        .tilePixelWidth = 16,
        .tilePixelHeight = 16,
        .tiles = tiles,
        .textureId = textureId,
    });
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto view = parseTilesetPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->tileCount, 2U);
    auto tile0 = view->tile(0);
    ASSERT_TRUE(tile0.has_value());
    EXPECT_EQ(tile0->localId, 1U);
    EXPECT_EQ(tile0->materialFlags, TilesetWire::MaterialSolid);
    EXPECT_FLOAT_EQ(tile0->u1, 0.5f);

    auto cooked = writeCookedTilesetAsset(tilesetId, TilesetPayloadDesc{
                                                         .tilePixelWidth = 16,
                                                         .tilePixelHeight = 16,
                                                         .tiles = tiles,
                                                         .textureId = textureId,
                                                     });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value());
    EXPECT_EQ(asset->header().assetKind, AssetKind::Tileset);
    EXPECT_EQ(asset->header().dependencyCount, 1U);
    auto dep = asset->dependency(0);
    ASSERT_TRUE(dep.has_value());
    EXPECT_EQ(dep->expectedKind, AssetKind::Texture2D);
}

TEST(TileMapPayloadTests, WriteParseAndCookedRoundTrip)
{
    const auto tilesetId = *Core::AssetId::fromBytes(idBytes(5U));
    const auto mapId = *Core::AssetId::fromBytes(idBytes(6U));
    const std::array<Core::u16, 6> tiles{0, 1, 2, 1, 0, 2};
    auto payload = writeTileMapPayloadBytes(TileMapPayloadDesc{
        .widthCells = 3,
        .heightCells = 2,
        .cellSizeMeters = 0.5f,
        .tiles = tiles,
        .tilesetId = tilesetId,
    });
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto view = parseTileMapPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->widthCells, 3U);
    EXPECT_EQ(view->heightCells, 2U);
    EXPECT_EQ(view->tileCount, 6U);
    EXPECT_EQ(*view->tileAt(1, 0), 1U);
    EXPECT_EQ(*view->tileAt(2, 1), 2U);

    auto cooked = writeCookedTileMapAsset(mapId, TileMapPayloadDesc{
                                                     .widthCells = 3,
                                                     .heightCells = 2,
                                                     .cellSizeMeters = 0.5f,
                                                     .tiles = tiles,
                                                     .tilesetId = tilesetId,
                                                 });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value());
    EXPECT_EQ(asset->header().assetKind, AssetKind::TileMap);
    auto dep = asset->dependency(0);
    ASSERT_TRUE(dep.has_value());
    EXPECT_EQ(dep->expectedKind, AssetKind::Tileset);
    ASSERT_TRUE(verifyCookedAssetContentHash(*asset).has_value());
}

TEST(TileMapPayloadTests, RejectsDimensionMismatch)
{
    const std::array<Core::u16, 2> tiles{1, 2};
    auto payload = writeTileMapPayloadBytes(TileMapPayloadDesc{
        .widthCells = 2,
        .heightCells = 2,
        .tiles = tiles,
    });
    ASSERT_FALSE(payload.has_value());
    EXPECT_EQ(payload.error().code, AssetFormatErrorCode::InvalidLayout);
}

} // namespace
} // namespace Tina::AssetFormat
