#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/EnvironmentMapPayload.hpp>
#include <tina/core/id/AssetId.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace Tina::AssetFormat {
namespace {

struct EnvironmentMapFixture final {
    std::vector<std::byte> diffuse = std::vector<std::byte>(48U, std::byte{0x11});
    std::vector<std::byte> specular = std::vector<std::byte>(240U, std::byte{0x22});
    std::vector<std::byte> brdf = std::vector<std::byte>(8U, std::byte{0x33});

    [[nodiscard]] EnvironmentMapPayloadDesc desc() const noexcept
    {
        return EnvironmentMapPayloadDesc{
            .radiancePixelFormat = EnvironmentMapRadiancePixelFormat::Rgba16Float,
            .brdfPixelFormat = EnvironmentMapBrdfPixelFormat::Rg16Float,
            .diffuseFaceSize = 1,
            .specularFaceSize = 2,
            .specularMipCount = 2,
            .brdfWidth = 2,
            .brdfHeight = 1,
            .diffusePixels = diffuse,
            .specularPixels = specular,
            .brdfPixels = brdf,
        };
    }
};

[[nodiscard]] Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

void putU16(std::vector<std::byte>& bytes, std::size_t offset, Core::u16 value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void putU32(std::vector<std::byte>& bytes, std::size_t offset, Core::u32 value)
{
    for (std::size_t index = 0; index < 4U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

TEST(EnvironmentMapPayloadTests, WriteParseRoundTripPreservesPrebakedSectionOrder)
{
    EnvironmentMapFixture fixture;
    auto payload = writeEnvironmentMapPayloadBytes(fixture.desc());
    ASSERT_TRUE(payload.has_value()) << payload.error().message;
    ASSERT_EQ(payload->size(), 328U);

    EXPECT_EQ((*payload)[0], std::byte{0x01});
    EXPECT_EQ((*payload)[1], std::byte{0x00});
    EXPECT_EQ((*payload)[16], std::byte{0x30});
    EXPECT_EQ((*payload)[20], std::byte{0xF0});
    EXPECT_EQ((*payload)[24], std::byte{0x08});

    auto view = parseEnvironmentMapPayload(*payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->schemaVersion, EnvironmentMapWire::SchemaVersion);
    EXPECT_EQ(view->radiancePixelFormat, EnvironmentMapRadiancePixelFormat::Rgba16Float);
    EXPECT_EQ(view->brdfPixelFormat, EnvironmentMapBrdfPixelFormat::Rg16Float);
    EXPECT_EQ(view->diffuseFaceSize, 1U);
    EXPECT_EQ(view->specularFaceSize, 2U);
    EXPECT_EQ(view->specularMipCount, 2U);
    EXPECT_EQ(view->brdfWidth, 2U);
    EXPECT_EQ(view->brdfHeight, 1U);
    EXPECT_EQ(view->diffuseBytes, 48U);
    EXPECT_EQ(view->specularBytes, 240U);
    EXPECT_EQ(view->brdfBytes, 8U);
    EXPECT_EQ(view->diffusePixels.data(), payload->data() + EnvironmentMapWire::HeaderBytes);
    EXPECT_EQ(view->specularPixels.data(), view->diffusePixels.data() + view->diffuseBytes);
    EXPECT_EQ(view->brdfPixels.data(), view->specularPixels.data() + view->specularBytes);
    EXPECT_TRUE(std::ranges::equal(view->diffusePixels, fixture.diffuse));
    EXPECT_TRUE(std::ranges::equal(view->specularPixels, fixture.specular));
    EXPECT_TRUE(std::ranges::equal(view->brdfPixels, fixture.brdf));
}

TEST(EnvironmentMapPayloadTests, WriterRejectsIncompleteMipChainAndMismatchedImageBytes)
{
    EnvironmentMapFixture fixture;

    auto incomplete = fixture.desc();
    incomplete.specularMipCount = 1;
    auto incompleteResult = writeEnvironmentMapPayloadBytes(incomplete);
    ASSERT_FALSE(incompleteResult.has_value());
    EXPECT_EQ(incompleteResult.error().code, AssetFormatErrorCode::InvalidLayout);

    auto mismatched = fixture.desc();
    mismatched.specularPixels = std::span(fixture.specular).first(fixture.specular.size() - 1U);
    auto mismatchedResult = writeEnvironmentMapPayloadBytes(mismatched);
    ASSERT_FALSE(mismatchedResult.has_value());
    EXPECT_EQ(mismatchedResult.error().code, AssetFormatErrorCode::InvalidLayout);

    auto overflowing = fixture.desc();
    overflowing.diffuseFaceSize = (std::numeric_limits<Core::u16>::max)();
    auto overflowingResult = writeEnvironmentMapPayloadBytes(overflowing);
    ASSERT_FALSE(overflowingResult.has_value());
    EXPECT_EQ(overflowingResult.error().code, AssetFormatErrorCode::ArithmeticOverflow);
}

TEST(EnvironmentMapPayloadTests, ParserRejectsUnsupportedOrInconsistentWireFields)
{
    EnvironmentMapFixture fixture;
    auto payload = writeEnvironmentMapPayloadBytes(fixture.desc());
    ASSERT_TRUE(payload.has_value()) << payload.error().message;

    auto unsupportedSchema = *payload;
    putU16(unsupportedSchema, 0U, 2U);
    auto schemaResult = parseEnvironmentMapPayload(unsupportedSchema);
    ASSERT_FALSE(schemaResult.has_value());
    EXPECT_EQ(schemaResult.error().code, AssetFormatErrorCode::UnsupportedSchema);

    auto unsupportedFormat = *payload;
    putU16(unsupportedFormat, 2U, 2U);
    auto formatResult = parseEnvironmentMapPayload(unsupportedFormat);
    ASSERT_FALSE(formatResult.has_value());
    EXPECT_EQ(formatResult.error().code, AssetFormatErrorCode::UnsupportedValue);

    auto incompleteMips = *payload;
    putU16(incompleteMips, 10U, 1U);
    auto mipResult = parseEnvironmentMapPayload(incompleteMips);
    ASSERT_FALSE(mipResult.has_value());
    EXPECT_EQ(mipResult.error().code, AssetFormatErrorCode::InvalidLayout);

    auto wrongByteCount = *payload;
    putU32(wrongByteCount, 20U, 239U);
    auto byteCountResult = parseEnvironmentMapPayload(wrongByteCount);
    ASSERT_FALSE(byteCountResult.has_value());
    EXPECT_EQ(byteCountResult.error().code, AssetFormatErrorCode::InvalidLayout);

    auto nonZeroReserved = *payload;
    putU32(nonZeroReserved, 28U, 1U);
    auto reservedResult = parseEnvironmentMapPayload(nonZeroReserved);
    ASSERT_FALSE(reservedResult.has_value());
    EXPECT_EQ(reservedResult.error().code, AssetFormatErrorCode::InvalidLayout);

    auto trailingBytes = *payload;
    trailingBytes.push_back(std::byte{0});
    auto trailingResult = parseEnvironmentMapPayload(trailingBytes);
    ASSERT_FALSE(trailingResult.has_value());
    EXPECT_EQ(trailingResult.error().code, AssetFormatErrorCode::InvalidLayout);

    auto truncatedBytes = *payload;
    truncatedBytes.pop_back();
    auto truncatedResult = parseEnvironmentMapPayload(truncatedBytes);
    ASSERT_FALSE(truncatedResult.has_value());
    EXPECT_EQ(truncatedResult.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(EnvironmentMapPayloadTests, CookedRoundTripUsesEnvironmentMapKindAndCurrentVersion)
{
    EnvironmentMapFixture fixture;
    const auto assetId = *Core::AssetId::fromBytes(idBytes(13U));
    auto cooked = writeCookedEnvironmentMapAsset(assetId, fixture.desc());
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;

    auto asset = parseCookedAssetView(*cooked);
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->header().assetKind, AssetKind::EnvironmentMap);
    EXPECT_EQ(asset->header().assetTypeVersion, EnvironmentMapWire::SchemaVersion);
    EXPECT_EQ(asset->header().dependencyCount, 0U);
    ASSERT_TRUE(verifyCookedAssetContentHash(*asset).has_value());

    auto environment = parseEnvironmentMapPayload(asset->payload());
    ASSERT_TRUE(environment.has_value()) << environment.error().message;
    EXPECT_EQ(environment->specularMipCount, 2U);
    EXPECT_EQ(environment->brdfPixels.front(), std::byte{0x33});
}

} // namespace
} // namespace Tina::AssetFormat
