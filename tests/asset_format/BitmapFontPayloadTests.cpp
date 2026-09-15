#include <tina/asset_format/BitmapFontPayload.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include "support/BitmapFontTestSupport.hpp"
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <limits>

namespace Tina::Tests {
namespace {
using namespace AssetFormat;
using namespace BitmapFontFixture;

void writeU32(std::vector<std::byte>& bytes, Core::usize offset, Core::u32 value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes[offset++] = static_cast<std::byte>((value >> shift) & 255U);
}

TEST(BitmapFontPayloadTests, RoundTripOwnsMetricsAndCanonicalRequiredPageDependencies)
{
    auto payload = writeBitmapFontPayloadBytes(font());
    ASSERT_TRUE(payload);
    EXPECT_EQ(payload->size(), BitmapFontWire::HeaderBytes + 2 * BitmapFontWire::PageBytes +
              5 * BitmapFontWire::GlyphBytes + BitmapFontWire::KerningBytes);
    const auto parsed = parseBitmapFontPayload(*payload);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(writeBitmapFontPayloadBytes(*parsed).value(), *payload);
    payload->clear();
    EXPECT_TRUE(parsed->contains(0x4E2D));
    EXPECT_FLOAT_EQ(parsed->kerning('A', 'V'), -1);
    const std::array ids{assetId(1), assetId(2)};
    const auto cooked = writeCookedBitmapFontAsset(assetId(3), *parsed, ids);
    ASSERT_TRUE(cooked);
    const auto file = parseCookedAssetView(*cooked);
    ASSERT_TRUE(file);
    EXPECT_EQ(file->header().assetKind, AssetKind::Font);
    EXPECT_EQ(file->header().assetTypeVersion, 1U);
    ASSERT_EQ(file->header().dependencyCount, 2U);
    EXPECT_EQ(file->dependency(1)->assetId, ids[1]);
    EXPECT_EQ(file->dependency(1)->expectedKind, AssetKind::Texture2D);
    EXPECT_EQ(file->dependency(1)->flags, DependencyFlags::Required);
    EXPECT_TRUE(verifyCookedAssetContentHash(*file));
    EXPECT_FALSE(writeCookedBitmapFontAsset(assetId(3), *parsed, std::array{ids[1], ids[0]}));
    EXPECT_FALSE(writeCookedBitmapFontAsset(assetId(3), *parsed, std::array{ids[0], ids[0]}));
    EXPECT_FALSE(writeCookedBitmapFontAsset(ids[0], *parsed, ids));
}

TEST(BitmapFontPayloadTests, RejectsWrongSchemaCountsLengthsKindsAndMetrics)
{
    const auto valid = writeBitmapFontPayloadBytes(font()).value();
    auto changed = valid; writeU32(changed, 0, 0);
    EXPECT_EQ(parseBitmapFontPayload(changed).error().code, AssetFormatErrorCode::UnsupportedSchema);
    changed = valid; writeU32(changed, 4, Text::BitmapFont::MaxPages + 1);
    EXPECT_FALSE(parseBitmapFontPayload(changed));
    changed = valid; changed.pop_back();
    EXPECT_FALSE(parseBitmapFontPayload(changed));
    changed = valid; changed.push_back(std::byte{0});
    EXPECT_FALSE(parseBitmapFontPayload(changed));
    changed = valid; writeU32(changed, BitmapFontWire::HeaderBytes + 8, 9);
    EXPECT_FALSE(parseBitmapFontPayload(changed));
    changed = valid; writeU32(changed, 16, std::bit_cast<Core::u32>(std::numeric_limits<float>::infinity()));
    EXPECT_FALSE(parseBitmapFontPayload(changed));
    changed = valid; writeU32(changed, 28, 'Z');
    EXPECT_FALSE(parseBitmapFontPayload(changed));
}

TEST(BitmapFontPayloadTests, FontPagesRequirePointClampSrgbAndOneMip)
{
    const auto source = pixels();
    const std::array levels{Texture2DLevelDesc{16, 16, std::as_bytes(std::span{source[0]})}};
    const auto bytes = writeTexture2DPayloadBytes({
        .sampler = {.minFilter = Texture2DFilterMode::Point, .magFilter = Texture2DFilterMode::Point,
                    .mipFilter = Texture2DMipFilterMode::None}, .levels = levels});
    ASSERT_TRUE(bytes);
    const auto texture = parseTexture2DPayload(*bytes);
    ASSERT_TRUE(texture);
    const auto page = font().descriptor().pages[0];
    EXPECT_TRUE(validateBitmapFontTexturePage(page, *texture));
    auto changed = *texture; changed.sampler.magFilter = Texture2DFilterMode::Linear;
    EXPECT_FALSE(validateBitmapFontTexturePage(page, changed));
    changed = *texture; changed.colorSpace = Texture2DColorSpace::Linear;
    EXPECT_FALSE(validateBitmapFontTexturePage(page, changed));
    changed = *texture; changed.levelCount = 2;
    EXPECT_FALSE(validateBitmapFontTexturePage(page, changed));
    changed = *texture; changed.sampler.wrapU = Texture2DWrapMode::Repeat;
    EXPECT_FALSE(validateBitmapFontTexturePage(page, changed));
    changed = *texture; changed.width = 8;
    EXPECT_FALSE(validateBitmapFontTexturePage(page, changed));
}

} // namespace
} // namespace Tina::Tests
