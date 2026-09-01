#include <tina/asset/TextureMipChain.hpp>

#include <tina/asset/AssetErrors.hpp>
#include <tina/asset_format/Texture2DPayload.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <vector>

namespace {

using namespace Tina;
using Asset::buildTexture2DMipChainRgba8;
using AssetFormat::Texture2DColorSpace;

[[nodiscard]] std::vector<std::byte> solidRgba8(Core::u16 width, Core::u16 height, Core::u8 red,
                                               Core::u8 green, Core::u8 blue, Core::u8 alpha)
{
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t index = 0; index < pixels.size(); index += 4U)
    {
        pixels[index] = static_cast<std::byte>(red);
        pixels[index + 1U] = static_cast<std::byte>(green);
        pixels[index + 2U] = static_cast<std::byte>(blue);
        pixels[index + 3U] = static_cast<std::byte>(alpha);
    }
    return pixels;
}

[[nodiscard]] Core::u8 channelAt(const Asset::Texture2DMipChainRgba8& chain, Core::u8 level,
                                 std::size_t pixelIndex, std::size_t channel)
{
    const auto& descriptor = chain.levels[level];
    return static_cast<Core::u8>(
        chain.bytes[descriptor.byteOffset + pixelIndex * 4U + channel]);
}

TEST(TextureMipChainTests, BuildsCompleteChainDownToOnePixel)
{
    const auto pixels = solidRgba8(8, 4, 10, 20, 30, 255);
    auto chain = buildTexture2DMipChainRgba8(8, 4, pixels, Texture2DColorSpace::Linear);
    ASSERT_TRUE(chain.has_value()) << chain.error().message;

    // 8x4 halves to 4x2, 2x1, 1x1 -- the chain length is driven by the larger axis.
    ASSERT_EQ(chain->levelCount, AssetFormat::texture2DFullMipLevelCount(8, 4));
    ASSERT_EQ(chain->levelCount, 4);

    const std::array<std::pair<Core::u16, Core::u16>, 4> expected{
        {{8, 4}, {4, 2}, {2, 1}, {1, 1}}};
    Core::u32 walkingOffset = 0;
    for (Core::u8 index = 0; index < chain->levelCount; ++index)
    {
        EXPECT_EQ(chain->levels[index].width, expected[index].first) << "level " << index;
        EXPECT_EQ(chain->levels[index].height, expected[index].second) << "level " << index;
        EXPECT_EQ(chain->levels[index].byteOffset, walkingOffset) << "level " << index;
        walkingOffset += chain->levels[index].byteSize;
    }
    EXPECT_EQ(walkingOffset, chain->bytes.size());
}

TEST(TextureMipChainTests, ChainFeedsThePayloadWriterAndRoundTrips)
{
    const auto pixels = solidRgba8(4, 4, 200, 100, 50, 255);
    auto chain = buildTexture2DMipChainRgba8(4, 4, pixels, Texture2DColorSpace::Srgb);
    ASSERT_TRUE(chain.has_value()) << chain.error().message;

    std::array<AssetFormat::Texture2DLevelDesc, AssetFormat::Texture2DWire::MaxLevelCount> storage{};
    const auto levels = chain->fillLevelDescs(storage);
    ASSERT_EQ(levels.size(), chain->levelCount);

    auto written = AssetFormat::writeTexture2DPayloadBytes(AssetFormat::Texture2DPayloadDesc{
        .pixelFormat = AssetFormat::Texture2DPixelFormat::Rgba8Unorm,
        .colorSpace = Texture2DColorSpace::Srgb,
        .sampler = {.mipFilter = AssetFormat::Texture2DMipFilterMode::Linear},
        .levels = levels,
    });
    ASSERT_TRUE(written.has_value()) << written.error().message;

    auto view = AssetFormat::parseTexture2DPayload(*written);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->levelCount, chain->levelCount);
    EXPECT_EQ(view->width, 4);
    EXPECT_EQ(view->height, 4);
    EXPECT_EQ(view->levels().back().width, 1);
    EXPECT_EQ(view->levels().back().height, 1);
}

// A solid colour must survive downsampling exactly. Averaging gamma-encoded bytes
// directly would pull a mid-grey toward black, which is the classic darkening mip.
TEST(TextureMipChainTests, SrgbDownsamplePreservesASolidMidGrey)
{
    constexpr Core::u8 MidGrey = 128;
    const auto pixels = solidRgba8(4, 4, MidGrey, MidGrey, MidGrey, 255);
    auto chain = buildTexture2DMipChainRgba8(4, 4, pixels, Texture2DColorSpace::Srgb);
    ASSERT_TRUE(chain.has_value()) << chain.error().message;

    for (Core::u8 level = 0; level < chain->levelCount; ++level)
    {
        EXPECT_NEAR(channelAt(*chain, level, 0, 0), MidGrey, 1) << "level " << level;
        EXPECT_NEAR(channelAt(*chain, level, 0, 1), MidGrey, 1) << "level " << level;
        EXPECT_NEAR(channelAt(*chain, level, 0, 2), MidGrey, 1) << "level " << level;
        EXPECT_EQ(channelAt(*chain, level, 0, 3), 255) << "level " << level;
    }
}

// Averaging black and white in sRGB space yields ~187, not the ~128 a naive byte mean
// gives: the linear midpoint of 0 and 1 re-encodes above the halfway byte. This pins
// that the transfer function is actually applied rather than approximated away.
TEST(TextureMipChainTests, SrgbDownsampleAveragesInLinearSpace)
{
    std::vector<std::byte> pixels(2U * 1U * 4U);
    pixels[0] = std::byte{0};
    pixels[1] = std::byte{0};
    pixels[2] = std::byte{0};
    pixels[3] = std::byte{255};
    pixels[4] = std::byte{255};
    pixels[5] = std::byte{255};
    pixels[6] = std::byte{255};
    pixels[7] = std::byte{255};

    auto srgb = buildTexture2DMipChainRgba8(2, 1, pixels, Texture2DColorSpace::Srgb);
    ASSERT_TRUE(srgb.has_value()) << srgb.error().message;
    ASSERT_EQ(srgb->levelCount, 2);
    EXPECT_NEAR(channelAt(*srgb, 1, 0, 0), 188, 2);

    // The same pixels tagged linear must average to the byte midpoint instead, so the
    // two paths cannot be silently sharing one formula.
    auto linear = buildTexture2DMipChainRgba8(2, 1, pixels, Texture2DColorSpace::Linear);
    ASSERT_TRUE(linear.has_value()) << linear.error().message;
    EXPECT_NEAR(channelAt(*linear, 1, 0, 0), 128, 1);
}

// A transparent pixel's RGB is arbitrary. A straight mean would drag that red into the
// visible half and show up as a halo around the opaque content.
TEST(TextureMipChainTests, TransparentPixelColorDoesNotBleedIntoTheAverage)
{
    std::vector<std::byte> pixels(2U * 1U * 4U);
    pixels[0] = std::byte{255}; // fully transparent red
    pixels[1] = std::byte{0};
    pixels[2] = std::byte{0};
    pixels[3] = std::byte{0};
    pixels[4] = std::byte{0}; // opaque green
    pixels[5] = std::byte{255};
    pixels[6] = std::byte{0};
    pixels[7] = std::byte{255};

    auto chain = buildTexture2DMipChainRgba8(2, 1, pixels, Texture2DColorSpace::Linear);
    ASSERT_TRUE(chain.has_value()) << chain.error().message;
    ASSERT_EQ(chain->levelCount, 2);

    EXPECT_EQ(channelAt(*chain, 1, 0, 0), 0);
    EXPECT_EQ(channelAt(*chain, 1, 0, 1), 255);
    EXPECT_EQ(channelAt(*chain, 1, 0, 2), 0);
    // Coverage still halves even though the colour came from one pixel only.
    EXPECT_NEAR(channelAt(*chain, 1, 0, 3), 128, 1);
}

TEST(TextureMipChainTests, FullyTransparentCoverageStaysZeroInsteadOfNaN)
{
    const auto pixels = solidRgba8(2, 2, 200, 150, 100, 0);
    auto chain = buildTexture2DMipChainRgba8(2, 2, pixels, Texture2DColorSpace::Srgb);
    ASSERT_TRUE(chain.has_value()) << chain.error().message;

    for (Core::u8 channel = 0; channel < 4; ++channel)
    {
        EXPECT_EQ(channelAt(*chain, 1, 0, channel), 0) << "channel " << channel;
    }
}

// Halving an odd extent floors, so the trailing column belongs to no destination pixel
// under a plain 2x2 box. It must still contribute or the right edge vanishes from every
// smaller level.
TEST(TextureMipChainTests, OddExtentKeepsTheTrailingEdgeContent)
{
    std::vector<std::byte> pixels(3U * 1U * 4U);
    for (std::size_t index = 0; index < 2U; ++index)
    {
        pixels[index * 4U] = std::byte{0};
        pixels[index * 4U + 1U] = std::byte{0};
        pixels[index * 4U + 2U] = std::byte{0};
        pixels[index * 4U + 3U] = std::byte{255};
    }
    pixels[8] = std::byte{255}; // the trailing column a floored box would discard
    pixels[9] = std::byte{255};
    pixels[10] = std::byte{255};
    pixels[11] = std::byte{255};

    auto chain = buildTexture2DMipChainRgba8(3, 1, pixels, Texture2DColorSpace::Linear);
    ASSERT_TRUE(chain.has_value()) << chain.error().message;
    ASSERT_EQ(chain->levels[1].width, 1);
    // Three taps averaged: two black and one white.
    EXPECT_NEAR(channelAt(*chain, 1, 0, 0), 85, 1);
}

TEST(TextureMipChainTests, SinglePixelBaseYieldsOneLevel)
{
    const auto pixels = solidRgba8(1, 1, 1, 2, 3, 4);
    auto chain = buildTexture2DMipChainRgba8(1, 1, pixels, Texture2DColorSpace::Linear);
    ASSERT_TRUE(chain.has_value()) << chain.error().message;
    EXPECT_EQ(chain->levelCount, 1);
    EXPECT_EQ(chain->bytes.size(), 4U);
}

TEST(TextureMipChainTests, RejectsMismatchedPixelCountAndBadExtent)
{
    const auto pixels = solidRgba8(4, 4, 0, 0, 0, 255);

    auto shortPixels = buildTexture2DMipChainRgba8(
        4, 4, std::span<const std::byte>{pixels}.first(pixels.size() - 4U),
        Texture2DColorSpace::Srgb);
    ASSERT_FALSE(shortPixels.has_value());
    EXPECT_EQ(shortPixels.error().code, Asset::AssetErrorCode::InvalidCatalogConfig);

    auto zeroExtent = buildTexture2DMipChainRgba8(0, 4, pixels, Texture2DColorSpace::Srgb);
    ASSERT_FALSE(zeroExtent.has_value());
    EXPECT_EQ(zeroExtent.error().code, Asset::AssetErrorCode::InvalidCatalogConfig);

    auto badColorSpace = buildTexture2DMipChainRgba8(4, 4, pixels, Texture2DColorSpace::Invalid);
    ASSERT_FALSE(badColorSpace.has_value());
    EXPECT_EQ(badColorSpace.error().code, Asset::AssetErrorCode::InvalidCatalogConfig);
}

// The chain owns its bytes, so a move must not leave the level table pointing at the
// buffer the move took. This is why levels are stored as offsets.
TEST(TextureMipChainTests, LevelDescriptorsSurviveAMove)
{
    const auto pixels = solidRgba8(4, 4, 12, 34, 56, 255);
    auto built = buildTexture2DMipChainRgba8(4, 4, pixels, Texture2DColorSpace::Linear);
    ASSERT_TRUE(built.has_value()) << built.error().message;

    Asset::Texture2DMipChainRgba8 moved = std::move(*built);
    std::array<AssetFormat::Texture2DLevelDesc, AssetFormat::Texture2DWire::MaxLevelCount> storage{};
    const auto levels = moved.fillLevelDescs(storage);

    ASSERT_EQ(levels.size(), moved.levelCount);
    EXPECT_EQ(levels.front().bytes.data(), moved.bytes.data());
    EXPECT_EQ(static_cast<Core::u8>(levels.front().bytes[0]), 12);
    EXPECT_EQ(static_cast<Core::u8>(levels.front().bytes[1]), 34);
}

} // namespace
