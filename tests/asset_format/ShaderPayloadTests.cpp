#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/ShaderPayload.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace Tina::AssetFormat {
namespace {

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x3CU);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] std::vector<std::byte> binary(Core::u8 seed, Core::usize count)
{
    std::vector<std::byte> bytes(count, std::byte{0});
    for (Core::usize index = 0; index < count; ++index)
    {
        bytes[index] = static_cast<std::byte>(static_cast<Core::u8>(seed + index));
    }
    return bytes;
}

[[nodiscard]] Core::u16 readU16(std::span<const std::byte> bytes, Core::usize offset)
{
    return static_cast<Core::u16>(std::to_integer<Core::u16>(bytes[offset]) |
                                  static_cast<Core::u16>(std::to_integer<Core::u16>(bytes[offset + 1U]) << 8U));
}

[[nodiscard]] Core::u32 readU32(std::span<const std::byte> bytes, Core::usize offset)
{
    Core::u32 value = 0;
    for (Core::usize index = 0; index < 4U; ++index)
    {
        value |= static_cast<Core::u32>(std::to_integer<Core::u8>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

void writeU16(std::vector<std::byte>& bytes, Core::usize offset, Core::u16 value)
{
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void writeU32(std::vector<std::byte>& bytes, Core::usize offset, Core::u32 value)
{
    for (Core::usize index = 0; index < 4U; ++index)
    {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

TEST(ShaderPayloadTests, RoundTripsBlobTableAndCookedIdentity)
{
    const std::vector<std::byte> glsl = binary(0x10U, 24U);
    const std::vector<std::byte> spirv = binary(0x40U, 40U);
    const std::array<ShaderBlobDesc, 2> blobs{
        ShaderBlobDesc{.profile = ShaderBinaryProfile::Glsl120, .bytes = glsl},
        ShaderBlobDesc{.profile = ShaderBinaryProfile::SpirV, .bytes = spirv},
    };
    const ShaderPayloadDesc desc{
        .shaderKind = ShaderKind::Sprite2D,
        .stage = ShaderStage::Fragment,
        .blobs = blobs,
    };

    auto payload = writeShaderPayloadBytes(desc);
    ASSERT_TRUE(payload) << payload.error().message;
    ASSERT_EQ(payload->size(), ShaderWire::HeaderBytes + (2U * ShaderWire::BlobEntryBytes) + 64U);
    EXPECT_EQ(readU32(*payload, 8U), 64U);

    auto view = parseShaderPayload(*payload);
    ASSERT_TRUE(view) << view.error().message;
    EXPECT_EQ(view->schemaVersion, ShaderWire::SchemaVersion);
    EXPECT_EQ(view->shaderKind, ShaderKind::Sprite2D);
    EXPECT_EQ(view->stage, ShaderStage::Fragment);
    ASSERT_EQ(view->blobCount, 2U);
    EXPECT_EQ(view->blobs()[0].profile, ShaderBinaryProfile::Glsl120);
    EXPECT_EQ(view->blobs()[1].profile, ShaderBinaryProfile::SpirV);
    EXPECT_TRUE(std::ranges::equal(view->blobs()[0].bytes, glsl));
    EXPECT_TRUE(std::ranges::equal(view->blobs()[1].bytes, spirv));
    EXPECT_TRUE(std::ranges::equal(view->blobForProfile(ShaderBinaryProfile::SpirV), spirv));
    EXPECT_TRUE(view->blobForProfile(ShaderBinaryProfile::Dxbc50).empty());

    const Core::AssetId id = assetId(0x5AU);
    auto cooked = writeCookedShaderAsset(id, desc);
    ASSERT_TRUE(cooked) << cooked.error().message;
    auto file = parseCookedAssetView(*cooked);
    ASSERT_TRUE(file) << file.error().message;
    EXPECT_EQ(file->header().assetKind, AssetKind::Shader);
    EXPECT_EQ(file->header().assetTypeVersion, ShaderWire::SchemaVersion);
    EXPECT_EQ(file->header().assetId, id);
    EXPECT_EQ(file->header().dependencyCount, 0U);
    EXPECT_TRUE(verifyCookedAssetContentHash(*file));
}

// The same binaries must produce the same bytes, or the content hash changes per cook and
// every downstream incremental check sees a false edit.
TEST(ShaderPayloadTests, EncodingIsCanonicalForAGivenBinarySet)
{
    const std::vector<std::byte> dxbc = binary(0x70U, 12U);
    const std::array<ShaderBlobDesc, 1> blobs{
        ShaderBlobDesc{.profile = ShaderBinaryProfile::Dxbc50, .bytes = dxbc},
    };
    const ShaderPayloadDesc desc{
        .shaderKind = ShaderKind::Mesh3D,
        .stage = ShaderStage::Fragment,
        .blobs = blobs,
    };
    auto first = writeShaderPayloadBytes(desc);
    auto second = writeShaderPayloadBytes(desc);
    ASSERT_TRUE(first) << first.error().message;
    ASSERT_TRUE(second) << second.error().message;
    EXPECT_EQ(*first, *second);
}

TEST(ShaderPayloadTests, RejectsUnsupportedKindStageAndProfile)
{
    const std::vector<std::byte> blob = binary(0x01U, 8U);
    std::array<ShaderBlobDesc, 1> blobs{
        ShaderBlobDesc{.profile = ShaderBinaryProfile::Glsl120, .bytes = blob},
    };
    ShaderPayloadDesc desc{
        .shaderKind = ShaderKind::Sprite2D,
        .stage = ShaderStage::Fragment,
        .blobs = blobs,
    };
    ASSERT_TRUE(writeShaderPayloadBytes(desc));

    desc.shaderKind = ShaderKind::Invalid;
    auto invalidKind = writeShaderPayloadBytes(desc);
    ASSERT_FALSE(invalidKind);
    EXPECT_EQ(invalidKind.error().code, AssetFormatErrorCode::UnsupportedValue);

    desc.shaderKind = ShaderKind::Sprite2D;
    desc.stage = ShaderStage::Invalid;
    auto invalidStage = writeShaderPayloadBytes(desc);
    ASSERT_FALSE(invalidStage);
    EXPECT_EQ(invalidStage.error().code, AssetFormatErrorCode::UnsupportedValue);

    desc.stage = ShaderStage::Fragment;
    blobs[0].profile = ShaderBinaryProfile::Invalid;
    desc.blobs = blobs;
    auto invalidProfile = writeShaderPayloadBytes(desc);
    ASSERT_FALSE(invalidProfile);
    EXPECT_EQ(invalidProfile.error().code, AssetFormatErrorCode::UnsupportedValue);
}

TEST(ShaderPayloadTests, RejectsEmptyDuplicateAndUnsortedBlobTables)
{
    const std::vector<std::byte> blob = binary(0x02U, 8U);
    ShaderPayloadDesc empty{
        .shaderKind = ShaderKind::Sprite2D,
        .stage = ShaderStage::Fragment,
        .blobs = {},
    };
    auto noBlobs = writeShaderPayloadBytes(empty);
    ASSERT_FALSE(noBlobs);
    EXPECT_EQ(noBlobs.error().code, AssetFormatErrorCode::InvalidLayout);

    const std::array<ShaderBlobDesc, 2> duplicate{
        ShaderBlobDesc{.profile = ShaderBinaryProfile::SpirV, .bytes = blob},
        ShaderBlobDesc{.profile = ShaderBinaryProfile::SpirV, .bytes = blob},
    };
    auto duplicated = writeShaderPayloadBytes(ShaderPayloadDesc{
        .shaderKind = ShaderKind::Sprite2D,
        .stage = ShaderStage::Fragment,
        .blobs = duplicate,
    });
    ASSERT_FALSE(duplicated);
    EXPECT_EQ(duplicated.error().code, AssetFormatErrorCode::InvalidLayout);

    const std::array<ShaderBlobDesc, 2> unsorted{
        ShaderBlobDesc{.profile = ShaderBinaryProfile::SpirV, .bytes = blob},
        ShaderBlobDesc{.profile = ShaderBinaryProfile::Glsl120, .bytes = blob},
    };
    auto outOfOrder = writeShaderPayloadBytes(ShaderPayloadDesc{
        .shaderKind = ShaderKind::Sprite2D,
        .stage = ShaderStage::Fragment,
        .blobs = unsorted,
    });
    ASSERT_FALSE(outOfOrder);
    EXPECT_EQ(outOfOrder.error().code, AssetFormatErrorCode::InvalidLayout);

    const std::array<ShaderBlobDesc, 1> emptyBinary{
        ShaderBlobDesc{.profile = ShaderBinaryProfile::Glsl120, .bytes = {}},
    };
    auto zeroBytes = writeShaderPayloadBytes(ShaderPayloadDesc{
        .shaderKind = ShaderKind::Sprite2D,
        .stage = ShaderStage::Fragment,
        .blobs = emptyBinary,
    });
    ASSERT_FALSE(zeroBytes);
    EXPECT_EQ(zeroBytes.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(ShaderPayloadTests, RejectsTruncatedSchemaAndReservedFields)
{
    const std::vector<std::byte> blob = binary(0x03U, 16U);
    const std::array<ShaderBlobDesc, 1> blobs{
        ShaderBlobDesc{.profile = ShaderBinaryProfile::Essl300, .bytes = blob},
    };
    auto payload = writeShaderPayloadBytes(ShaderPayloadDesc{
        .shaderKind = ShaderKind::Sprite2D,
        .stage = ShaderStage::Fragment,
        .blobs = blobs,
    });
    ASSERT_TRUE(payload) << payload.error().message;

    auto truncated = parseShaderPayload(std::span<const std::byte>{*payload}.first(ShaderWire::HeaderBytes - 1U));
    ASSERT_FALSE(truncated);
    EXPECT_EQ(truncated.error().code, AssetFormatErrorCode::InvalidHeader);

    auto badSchema = *payload;
    writeU16(badSchema, 0U, static_cast<Core::u16>(ShaderWire::SchemaVersion + 1U));
    auto schemaResult = parseShaderPayload(badSchema);
    ASSERT_FALSE(schemaResult);
    EXPECT_EQ(schemaResult.error().code, AssetFormatErrorCode::UnsupportedSchema);

    auto headerReserved = *payload;
    writeU32(headerReserved, 12U, 1U);
    auto headerReservedResult = parseShaderPayload(headerReserved);
    ASSERT_FALSE(headerReservedResult);
    EXPECT_EQ(headerReservedResult.error().code, AssetFormatErrorCode::InvalidLayout);

    auto entryReserved = *payload;
    writeU16(entryReserved, ShaderWire::HeaderBytes + 2U, 1U);
    auto entryReservedResult = parseShaderPayload(entryReserved);
    ASSERT_FALSE(entryReservedResult);
    EXPECT_EQ(entryReservedResult.error().code, AssetFormatErrorCode::InvalidLayout);

    auto shortTail = *payload;
    shortTail.pop_back();
    auto shortTailResult = parseShaderPayload(shortTail);
    ASSERT_FALSE(shortTailResult);
    EXPECT_EQ(shortTailResult.error().code, AssetFormatErrorCode::InvalidLayout);
}

// A blob offset inside the payload but not at the running position would let two entries
// alias the same bytes, which parses fine and hashes differently per cook.
TEST(ShaderPayloadTests, RejectsBlobOffsetsThatAreNotTightlyPacked)
{
    const std::vector<std::byte> first = binary(0x04U, 16U);
    const std::vector<std::byte> second = binary(0x50U, 16U);
    const std::array<ShaderBlobDesc, 2> blobs{
        ShaderBlobDesc{.profile = ShaderBinaryProfile::Glsl120, .bytes = first},
        ShaderBlobDesc{.profile = ShaderBinaryProfile::SpirV, .bytes = second},
    };
    auto payload = writeShaderPayloadBytes(ShaderPayloadDesc{
        .shaderKind = ShaderKind::Mesh3D,
        .stage = ShaderStage::Fragment,
        .blobs = blobs,
    });
    ASSERT_TRUE(payload) << payload.error().message;

    const Core::usize secondEntry = ShaderWire::HeaderBytes + ShaderWire::BlobEntryBytes;
    const Core::u32 packedOffset = readU32(*payload, secondEntry + 4U);
    ASSERT_EQ(packedOffset, ShaderWire::HeaderBytes + (2U * ShaderWire::BlobEntryBytes) + 16U);

    auto aliased = *payload;
    writeU32(aliased, secondEntry + 4U, ShaderWire::HeaderBytes + (2U * ShaderWire::BlobEntryBytes));
    auto aliasedResult = parseShaderPayload(aliased);
    ASSERT_FALSE(aliasedResult);
    EXPECT_EQ(aliasedResult.error().code, AssetFormatErrorCode::InvalidLayout);

    auto past = *payload;
    writeU32(past, secondEntry + 8U, static_cast<Core::u32>(past.size()));
    auto pastResult = parseShaderPayload(past);
    ASSERT_FALSE(pastResult);
    EXPECT_EQ(pastResult.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(ShaderPayloadTests, ProfileNamesRoundTripThroughTheRecipeSpelling)
{
    constexpr std::array<ShaderBinaryProfile, 4> profiles{
        ShaderBinaryProfile::Glsl120,
        ShaderBinaryProfile::SpirV,
        ShaderBinaryProfile::Dxbc50,
        ShaderBinaryProfile::Essl300,
    };
    for (const ShaderBinaryProfile profile : profiles)
    {
        const std::string_view name = shaderBinaryProfileName(profile);
        EXPECT_FALSE(name.empty());
        const auto parsed = parseShaderBinaryProfileName(name);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, profile);
    }
    EXPECT_TRUE(shaderBinaryProfileName(ShaderBinaryProfile::Invalid).empty());
    EXPECT_FALSE(parseShaderBinaryProfileName("").has_value());
    EXPECT_FALSE(parseShaderBinaryProfileName("glsl").has_value());
    EXPECT_FALSE(parseShaderBinaryProfileName("GLSL120").has_value());
}

} // namespace
} // namespace Tina::AssetFormat
