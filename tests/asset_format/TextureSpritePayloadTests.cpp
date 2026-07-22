#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/SpritePayload.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>
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

TEST(Texture2DPayloadTests, WriteParseRoundTrip)
{
    std::vector<std::byte> pixels(2U * 2U * 4U);
    for (std::size_t index = 0; index < pixels.size(); ++index)
    {
        pixels[index] = static_cast<std::byte>(index + 1U);
    }
    auto written = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .width = 2,
        .height = 2,
        .pixelFormat = Texture2DPixelFormat::Rgba8Unorm,
        .pixels = pixels,
    });
    ASSERT_TRUE(written.has_value()) << written.error().message;
    auto view = parseTexture2DPayload(*written);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->width, 2);
    EXPECT_EQ(view->height, 2);
    EXPECT_EQ(view->pixelBytes, 16U);
    EXPECT_EQ(view->pixels[0], std::byte{1});
    EXPECT_EQ(view->pixels[15], std::byte{16});
}

TEST(Texture2DPayloadTests, RejectsSizeMismatch)
{
    std::array<std::byte, 4> pixels{};
    auto written = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .width = 2,
        .height = 2,
        .pixels = pixels,
    });
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(SpritePayloadTests, WriteParseAndCookedRoundTrip)
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    const auto spriteId = *Core::AssetId::fromBytes(idBytes(3U));
    SpritePayloadDesc desc{
        .u0 = 0.0f,
        .v0 = 0.0f,
        .u1 = 0.5f,
        .v1 = 1.0f,
        .pivotX = 0.25f,
        .pivotY = 0.75f,
        .pixelsPerUnit = 32.0f,
        .textureId = textureId,
    };
    auto payload = writeSpritePayloadBytes(desc);
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    auto view = parseSpritePayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_FLOAT_EQ(view->u1, 0.5f);
    EXPECT_FLOAT_EQ(view->pixelsPerUnit, 32.0f);

    auto cooked = writeCookedSpriteAsset(spriteId, desc);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().assetKind, AssetKind::Sprite);
    EXPECT_EQ(asset->header().dependencyCount, 1U);
    auto dep = asset->dependency(0);
    ASSERT_TRUE(dep.has_value());
    EXPECT_EQ(dep->assetId, textureId);
    EXPECT_EQ(dep->expectedKind, AssetKind::Texture2D);
    auto spriteView = parseSpritePayload(asset->payload());
    ASSERT_TRUE(spriteView.has_value());
    EXPECT_FLOAT_EQ(spriteView->pivotX, 0.25f);
}

TEST(Texture2DPayloadTests, CookedTextureRoundTrip)
{
    const auto textureId = *Core::AssetId::fromBytes(idBytes(1U));
    std::vector<std::byte> pixels(1U * 1U * 4U, std::byte{0xFF});
    auto cooked = writeCookedTexture2DAsset(textureId, Texture2DPayloadDesc{
                                                           .width = 1,
                                                           .height = 1,
                                                           .pixels = pixels,
                                                       });
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value());
    EXPECT_EQ(asset->header().assetKind, AssetKind::Texture2D);
    auto view = parseTexture2DPayload(asset->payload());
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->width, 1);
    EXPECT_EQ(view->pixels[0], std::byte{0xFF});
    ASSERT_TRUE(verifyCookedAssetContentHash(*asset).has_value());
}

} // namespace
} // namespace Tina::AssetFormat
