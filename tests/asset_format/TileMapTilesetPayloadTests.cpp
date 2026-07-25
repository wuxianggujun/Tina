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
    const std::array tileProperties{
        TileMapPropertyDesc{.key = "role", .value = "visual"},
    };
    const std::array objectProperties{
        TileMapPropertyDesc{.key = "role", .value = "spawn"},
    };
    const std::array objects{
        TileMapObjectDesc{
            .stableObjectId = 101,
            .kind = TileMapObjectKind::Point,
            .visible = true,
            .name = "player_spawn",
            .x = 1.5f,
            .y = 0.5f,
            .properties = objectProperties,
        },
        TileMapObjectDesc{
            .stableObjectId = 102,
            .kind = TileMapObjectKind::Rectangle,
            .visible = false,
            .name = "trigger",
            .x = 0.5f,
            .y = 0.0f,
            .width = 1.0f,
            .height = 0.5f,
        },
    };
    const std::array layers{
        TileMapLayerDesc{
            .stableLayerId = 7,
            .kind = TileMapLayerKind::Tile,
            .visible = true,
            .name = "visual",
            .properties = tileProperties,
            .tiles = tiles,
        },
        TileMapLayerDesc{
            .stableLayerId = 8,
            .kind = TileMapLayerKind::Object,
            .visible = false,
            .name = "gameplay",
            .objects = objects,
        },
    };
    auto payload = writeTileMapPayloadBytes(TileMapPayloadDesc{
        .widthCells = 3,
        .heightCells = 2,
        .cellSizeMeters = 0.5f,
        .layers = layers,
        .tilesetId = tilesetId,
    });
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto view = parseTileMapPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->widthCells, 3U);
    EXPECT_EQ(view->heightCells, 2U);
    EXPECT_EQ(view->layerCount, 2U);
    const auto tileLayer = view->findLayer(7);
    ASSERT_TRUE(tileLayer.has_value());
    ASSERT_TRUE(view->layerAt(0).has_value());
    EXPECT_EQ(view->layerAt(0)->stableLayerId, 7U);
    EXPECT_EQ(view->layerAt(1)->stableLayerId, 8U);
    EXPECT_TRUE(tileLayer->visible);
    EXPECT_EQ(tileLayer->tileCount, 6U);
    EXPECT_EQ(*tileLayer->tileAt(1, 0), 1U);
    EXPECT_EQ(*tileLayer->tileAt(2, 1), 2U);
    const auto tileProperty = tileLayer->findProperty("role");
    ASSERT_TRUE(tileProperty.has_value());
    EXPECT_EQ(tileProperty->key, "role");
    EXPECT_EQ(tileProperty->value, "visual");
    const auto objectLayer = view->findLayer(8);
    ASSERT_TRUE(objectLayer.has_value());
    EXPECT_EQ(objectLayer->kind, TileMapLayerKind::Object);
    EXPECT_EQ(objectLayer->objectCount, 2U);
    const auto point = objectLayer->objectAt(0);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->stableObjectId, 101U);
    EXPECT_EQ(point->kind, TileMapObjectKind::Point);
    EXPECT_TRUE(point->visible);
    const auto objectProperty = point->findProperty("role");
    ASSERT_TRUE(objectProperty.has_value());
    EXPECT_EQ(objectProperty->value, "spawn");
    const auto rectangle = objectLayer->findObject(102);
    ASSERT_TRUE(rectangle.has_value());
    EXPECT_EQ(rectangle->kind, TileMapObjectKind::Rectangle);
    EXPECT_FALSE(rectangle->visible);
    EXPECT_FLOAT_EQ(rectangle->width, 1.0f);

    auto cooked = writeCookedTileMapAsset(mapId, TileMapPayloadDesc{
                                                     .widthCells = 3,
                                                     .heightCells = 2,
                                                     .cellSizeMeters = 0.5f,
                                                     .layers = layers,
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
    const std::array layers{
        TileMapLayerDesc{
            .stableLayerId = 1,
            .kind = TileMapLayerKind::Tile,
            .visible = true,
            .name = "visual",
            .tiles = tiles,
        },
    };
    auto payload = writeTileMapPayloadBytes(TileMapPayloadDesc{
        .widthCells = 2,
        .heightCells = 2,
        .layers = layers,
    });
    ASSERT_FALSE(payload.has_value());
    EXPECT_EQ(payload.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(TileMapPayloadTests, RejectsInvalidAndDuplicateStableIds)
{
    const std::array<Core::u16, 1> tiles{1};
    const std::array duplicateLayers{
        TileMapLayerDesc{.stableLayerId = 7, .kind = TileMapLayerKind::Tile, .tiles = tiles},
        TileMapLayerDesc{.stableLayerId = 7, .kind = TileMapLayerKind::Tile, .tiles = tiles},
    };
    auto duplicateLayerPayload = writeTileMapPayloadBytes(
        TileMapPayloadDesc{.widthCells = 1, .heightCells = 1, .layers = duplicateLayers});
    ASSERT_FALSE(duplicateLayerPayload.has_value());
    EXPECT_EQ(duplicateLayerPayload.error().code, AssetFormatErrorCode::InvalidIdentity);

    const std::array firstObjects{
        TileMapObjectDesc{.stableObjectId = 101, .kind = TileMapObjectKind::Point},
    };
    const std::array secondObjects{
        TileMapObjectDesc{.stableObjectId = 101, .kind = TileMapObjectKind::Rectangle, .width = 1.0f, .height = 1.0f},
    };
    const std::array duplicateObjects{
        TileMapLayerDesc{.stableLayerId = 10, .kind = TileMapLayerKind::Object, .objects = firstObjects},
        TileMapLayerDesc{.stableLayerId = 20, .kind = TileMapLayerKind::Object, .objects = secondObjects},
    };
    auto duplicateObjectPayload = writeTileMapPayloadBytes(
        TileMapPayloadDesc{.widthCells = 1, .heightCells = 1, .layers = duplicateObjects});
    ASSERT_FALSE(duplicateObjectPayload.has_value());
    EXPECT_EQ(duplicateObjectPayload.error().code, AssetFormatErrorCode::InvalidIdentity);

    const std::array zeroLayer{
        TileMapLayerDesc{.stableLayerId = 0, .kind = TileMapLayerKind::Tile, .tiles = tiles},
    };
    auto zeroLayerPayload =
        writeTileMapPayloadBytes(TileMapPayloadDesc{.widthCells = 1, .heightCells = 1, .layers = zeroLayer});
    ASSERT_FALSE(zeroLayerPayload.has_value());
    EXPECT_EQ(zeroLayerPayload.error().code, AssetFormatErrorCode::InvalidIdentity);
}

TEST(TileMapPayloadTests, ParserRejectsLegacySchemaAndDuplicateWireIds)
{
    const std::array<Core::u16, 1> tiles{1};
    const std::array layers{
        TileMapLayerDesc{.stableLayerId = 7, .kind = TileMapLayerKind::Tile, .tiles = tiles},
        TileMapLayerDesc{.stableLayerId = 8, .kind = TileMapLayerKind::Tile, .tiles = tiles},
    };
    auto payload = writeTileMapPayloadBytes(
        TileMapPayloadDesc{.widthCells = 1, .heightCells = 1, .layers = layers});
    ASSERT_TRUE(payload.has_value());

    auto legacy = *payload;
    legacy[0] = std::byte{1};
    legacy[1] = std::byte{0};
    auto legacyView = parseTileMapPayload(legacy);
    ASSERT_FALSE(legacyView.has_value());
    EXPECT_EQ(legacyView.error().code, AssetFormatErrorCode::UnsupportedSchema);

    // Header(20) + first tile layer header(16) + one u16 tile = second layer offset 38.
    auto duplicateLayer = *payload;
    duplicateLayer[38] = duplicateLayer[20];
    duplicateLayer[39] = duplicateLayer[21];
    duplicateLayer[40] = duplicateLayer[22];
    duplicateLayer[41] = duplicateLayer[23];
    auto duplicateLayerView = parseTileMapPayload(duplicateLayer);
    ASSERT_FALSE(duplicateLayerView.has_value());
    EXPECT_EQ(duplicateLayerView.error().code, AssetFormatErrorCode::InvalidIdentity);

    const std::array firstObjects{
        TileMapObjectDesc{.stableObjectId = 101, .kind = TileMapObjectKind::Point},
    };
    const std::array secondObjects{
        TileMapObjectDesc{.stableObjectId = 102, .kind = TileMapObjectKind::Point},
    };
    const std::array objectLayers{
        TileMapLayerDesc{.stableLayerId = 10, .kind = TileMapLayerKind::Object, .objects = firstObjects},
        TileMapLayerDesc{.stableLayerId = 20, .kind = TileMapLayerKind::Object, .objects = secondObjects},
    };
    auto objectPayload = writeTileMapPayloadBytes(
        TileMapPayloadDesc{.widthCells = 1, .heightCells = 1, .layers = objectLayers});
    ASSERT_TRUE(objectPayload.has_value());
    // Header(20) + first object layer(16 + 28) = second layer offset 64; object header begins at 80.
    (*objectPayload)[80] = (*objectPayload)[36];
    (*objectPayload)[81] = (*objectPayload)[37];
    (*objectPayload)[82] = (*objectPayload)[38];
    (*objectPayload)[83] = (*objectPayload)[39];
    auto duplicateObjectView = parseTileMapPayload(*objectPayload);
    ASSERT_FALSE(duplicateObjectView.has_value());
    EXPECT_EQ(duplicateObjectView.error().code, AssetFormatErrorCode::InvalidIdentity);
}

} // namespace
} // namespace Tina::AssetFormat
