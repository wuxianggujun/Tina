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
    std::array<std::byte, 32> level0{};
    std::array<std::byte, 8> level1{};
    std::array<std::byte, 4> level2{};
    for (std::size_t index = 0; index < level0.size(); ++index)
    {
        level0[index] = static_cast<std::byte>(index + 1U);
    }
    level1.fill(std::byte{0xA1});
    level2.fill(std::byte{0xB2});
    const std::array levels{
        Texture2DLevelDesc{.width = 4, .height = 2, .bytes = level0},
        Texture2DLevelDesc{.width = 2, .height = 1, .bytes = level1},
        Texture2DLevelDesc{.width = 1, .height = 1, .bytes = level2},
    };
    const Texture2DSamplerDesc sampler{
        .wrapU = Texture2DWrapMode::Border,
        .wrapV = Texture2DWrapMode::Mirror,
        .minFilter = Texture2DFilterMode::Anisotropic,
        .magFilter = Texture2DFilterMode::Anisotropic,
        .mipFilter = Texture2DMipFilterMode::Linear,
    };
    auto written = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .pixelFormat = Texture2DPixelFormat::Rgba8Unorm,
        .colorSpace = Texture2DColorSpace::Linear,
        .sampler = sampler,
        .levels = levels,
    });
    ASSERT_TRUE(written.has_value()) << written.error().message;
    auto view = parseTexture2DPayload(*written);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->schemaVersion, Texture2DWire::SchemaVersion);
    EXPECT_EQ(view->width, 4);
    EXPECT_EQ(view->height, 2);
    EXPECT_EQ(view->pixelFormat, Texture2DPixelFormat::Rgba8Unorm);
    EXPECT_EQ(view->colorSpace, Texture2DColorSpace::Linear);
    EXPECT_EQ(view->sampler, sampler);
    EXPECT_EQ(view->levelCount, 3U);
    EXPECT_EQ(view->levelBytes, 44U);
    ASSERT_EQ(view->levels().size(), 3U);
    EXPECT_EQ(view->levels()[0].width, 4U);
    EXPECT_EQ(view->levels()[0].height, 2U);
    EXPECT_EQ(view->levels()[0].bytes.front(), std::byte{1});
    EXPECT_EQ(view->levels()[0].bytes.back(), std::byte{32});
    EXPECT_EQ(view->levels()[1].bytes.front(), std::byte{0xA1});
    EXPECT_EQ(view->levels()[2].bytes.front(), std::byte{0xB2});
}

TEST(Texture2DPayloadTests, RejectsSizeMismatch)
{
    std::array<std::byte, 4> pixels{};
    const std::array levels{
        Texture2DLevelDesc{.width = 2, .height = 2, .bytes = pixels},
    };
    auto written = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .sampler = {.mipFilter = Texture2DMipFilterMode::None},
        .levels = levels,
    });
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(Texture2DPayloadTests, CompressedFormatSizesAndRoundTrips)
{
    struct FormatCase final {
        Texture2DPixelFormat format;
        Core::u32 expectedBytes;
    };
    constexpr std::array Cases{
        FormatCase{Texture2DPixelFormat::Bc1Rgba, 32U},
        FormatCase{Texture2DPixelFormat::Bc3Rgba, 64U},
        FormatCase{Texture2DPixelFormat::Bc7Rgba, 64U},
        FormatCase{Texture2DPixelFormat::Astc4x4Rgba, 64U},
    };

    for (const FormatCase& testCase : Cases)
    {
        SCOPED_TRACE(static_cast<Core::u16>(testCase.format));
        ASSERT_EQ(texture2DLevelByteSize(testCase.format, 7, 5), testCase.expectedBytes);
        std::vector<std::byte> bytes(testCase.expectedBytes, std::byte{0x5A});
        const std::array levels{
            Texture2DLevelDesc{.width = 7, .height = 5, .bytes = bytes},
        };
        auto written = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
            .pixelFormat = testCase.format,
            .colorSpace = Texture2DColorSpace::Srgb,
            .sampler = {.mipFilter = Texture2DMipFilterMode::None},
            .levels = levels,
        });
        ASSERT_TRUE(written.has_value()) << written.error().message;
        auto view = parseTexture2DPayload(*written);
        ASSERT_TRUE(view.has_value()) << view.error().message;
        EXPECT_EQ(view->pixelFormat, testCase.format);
        EXPECT_EQ(view->colorSpace, Texture2DColorSpace::Srgb);
        ASSERT_EQ(view->levels().size(), 1U);
        EXPECT_EQ(view->levels().front().bytes.size(), testCase.expectedBytes);
        EXPECT_EQ(view->levels().front().bytes.front(), std::byte{0x5A});
    }
}

TEST(Texture2DPayloadTests, OddCompressedMipChainRoundsDownAndKeepsWholeTailBlocks)
{
    std::array<std::byte, 32> level0{};
    std::array<std::byte, 8> level1{};
    std::array<std::byte, 8> level2{};
    level0.fill(std::byte{0x10});
    level1.fill(std::byte{0x21});
    level2.fill(std::byte{0x32});
    const std::array levels{
        Texture2DLevelDesc{.width = 7, .height = 5, .bytes = level0},
        Texture2DLevelDesc{.width = 3, .height = 2, .bytes = level1},
        Texture2DLevelDesc{.width = 1, .height = 1, .bytes = level2},
    };

    auto written = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .pixelFormat = Texture2DPixelFormat::Bc1Rgba,
        .sampler = {.mipFilter = Texture2DMipFilterMode::Point},
        .levels = levels,
    });
    ASSERT_TRUE(written.has_value()) << written.error().message;

    auto view = parseTexture2DPayload(*written);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    ASSERT_EQ(view->levels().size(), 3U);
    EXPECT_EQ(view->levels()[0].width, 7U);
    EXPECT_EQ(view->levels()[0].height, 5U);
    EXPECT_EQ(view->levels()[0].bytes.size(), 32U);
    EXPECT_EQ(view->levels()[1].width, 3U);
    EXPECT_EQ(view->levels()[1].height, 2U);
    EXPECT_EQ(view->levels()[1].bytes.size(), 8U);
    EXPECT_EQ(view->levels()[2].width, 1U);
    EXPECT_EQ(view->levels()[2].height, 1U);
    EXPECT_EQ(view->levels()[2].bytes.size(), 8U);
    EXPECT_EQ(view->levelBytes, 48U);
}

TEST(Texture2DPayloadTests, RejectsIncompleteMipChainsAndMipFilterMismatch)
{
    std::array<std::byte, 64> level0{};
    std::array<std::byte, 16> level1{};
    const std::array incompleteLevels{
        Texture2DLevelDesc{.width = 4, .height = 4, .bytes = level0},
        Texture2DLevelDesc{.width = 2, .height = 2, .bytes = level1},
    };
    auto incomplete = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .sampler = {.mipFilter = Texture2DMipFilterMode::Linear},
        .levels = incompleteLevels,
    });
    ASSERT_FALSE(incomplete.has_value());
    EXPECT_EQ(incomplete.error().code, AssetFormatErrorCode::InvalidLayout);

    const std::array singleLevel{
        Texture2DLevelDesc{.width = 4, .height = 4, .bytes = level0},
    };
    auto singleWithMipFilter = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .sampler = {.mipFilter = Texture2DMipFilterMode::Linear},
        .levels = singleLevel,
    });
    ASSERT_FALSE(singleWithMipFilter.has_value());
    EXPECT_EQ(singleWithMipFilter.error().code, AssetFormatErrorCode::InvalidLayout);

    std::array<std::byte, 4> level2{};
    const std::array completeLevels{
        Texture2DLevelDesc{.width = 4, .height = 4, .bytes = level0},
        Texture2DLevelDesc{.width = 2, .height = 2, .bytes = level1},
        Texture2DLevelDesc{.width = 1, .height = 1, .bytes = level2},
    };
    auto chainWithoutMipFilter = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .sampler = {.mipFilter = Texture2DMipFilterMode::None},
        .levels = completeLevels,
    });
    ASSERT_FALSE(chainWithoutMipFilter.has_value());
    EXPECT_EQ(chainWithoutMipFilter.error().code, AssetFormatErrorCode::InvalidLayout);

    auto asymmetricAnisotropic = writeTexture2DPayloadBytes(Texture2DPayloadDesc{
        .sampler =
            {
                .minFilter = Texture2DFilterMode::Anisotropic,
                .magFilter = Texture2DFilterMode::Linear,
                .mipFilter = Texture2DMipFilterMode::None,
            },
        .levels = singleLevel,
    });
    ASSERT_FALSE(asymmetricAnisotropic.has_value());
    EXPECT_EQ(asymmetricAnisotropic.error().code, AssetFormatErrorCode::InvalidLayout);
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
    auto cooked = writeCookedTexture2DAssetRgba8(textureId, 1, 1, pixels);
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value());
    EXPECT_EQ(asset->header().assetKind, AssetKind::Texture2D);
    EXPECT_EQ(asset->header().assetTypeVersion, Texture2DWire::SchemaVersion);
    auto view = parseTexture2DPayload(asset->payload());
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->width, 1);
    EXPECT_EQ(view->schemaVersion, Texture2DWire::SchemaVersion);
    EXPECT_EQ(view->sampler.mipFilter, Texture2DMipFilterMode::None);
    EXPECT_EQ(view->basePixels()[0], std::byte{0xFF});
    ASSERT_TRUE(verifyCookedAssetContentHash(*asset).has_value());
}

} // namespace
} // namespace Tina::AssetFormat
