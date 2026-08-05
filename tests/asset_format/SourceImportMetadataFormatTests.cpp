#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/asset_format/SourceImportMetadataFormat.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::AssetFormat {
namespace {

using Bytes = std::vector<std::byte>;

void putU8(Bytes& bytes, Core::usize offset, Core::u8 value)
{
    bytes.at(offset) = static_cast<std::byte>(value);
}

void putU16(Bytes& bytes, Core::usize offset, Core::u16 value)
{
    putU8(bytes, offset, static_cast<Core::u8>(value & 0xFFU));
    putU8(bytes, offset + 1U, static_cast<Core::u8>((value >> 8U) & 0xFFU));
}

void putU32(Bytes& bytes, Core::usize offset, Core::u32 value)
{
    for (Core::usize index = 0; index < 4U; ++index)
    {
        putU8(bytes, offset + index, static_cast<Core::u8>((value >> (index * 8U)) & 0xFFU));
    }
}

void putU64(Bytes& bytes, Core::usize offset, Core::u64 value)
{
    for (Core::usize index = 0; index < 8U; ++index)
    {
        putU8(bytes, offset + index, static_cast<Core::u8>((value >> (index * 8U)) & 0xFFU));
    }
}

template <Core::usize Size>
void putFixed(Bytes& bytes, Core::usize offset, const std::array<std::byte, Size>& value)
{
    std::copy(value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

Core::AssetId::Bytes assetIdBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

SourceImportUnitId::Bytes unitIdBytes(Core::u8 seed)
{
    SourceImportUnitId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xC3U);
    return bytes;
}

Core::ContentHash::Bytes hashBytes(Core::u8 seed)
{
    Core::ContentHash::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[7] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

Core::AssetId assetId(Core::u8 seed)
{
    return *Core::AssetId::fromBytes(assetIdBytes(seed));
}

SourceImportUnitId unitId(Core::u8 seed)
{
    return *SourceImportUnitId::fromBytes(unitIdBytes(seed));
}

Core::ContentHash contentHash(Core::u8 seed)
{
    return *Core::ContentHash::fromBytes(hashBytes(seed));
}

Bytes makeValidMetadata()
{
    const std::array sources{
        SourceImportMetadataWriteSource{
            .path = "characters/hero.glb",
            .contentHash = contentHash(0x10U),
            .fileBytes = 1024U,
            .readExtent = SourceImportReadExtent::WholeFile,
        },
        SourceImportMetadataWriteSource{
            .path = "shared/palette.png",
            .contentHash = contentHash(0x11U),
            .fileBytes = 256U,
            .readExtent = SourceImportReadExtent::Prefix,
        },
        SourceImportMetadataWriteSource{
            .path = "textures/hero.png",
            .contentHash = contentHash(0x12U),
            .fileBytes = 512U,
            .readExtent = SourceImportReadExtent::WholeFile,
        },
    };
    const std::array unitOneInputs{
        SourceImportMetadataWriteInput{
            .sourceIndex = 0U,
            .flags = SourceImportInputFlags::Primary,
        },
        SourceImportMetadataWriteInput{
            .sourceIndex = 1U,
            .flags = SourceImportInputFlags::None,
        },
    };
    const std::array unitTwoInputs{
        SourceImportMetadataWriteInput{
            .sourceIndex = 0U,
            .flags = SourceImportInputFlags::None,
        },
        SourceImportMetadataWriteInput{
            .sourceIndex = 2U,
            .flags = SourceImportInputFlags::Primary,
        },
    };
    const std::array unitOneOutputs{
        SourceImportMetadataWriteOutput{
            .assetId = assetId(0x10U),
            .assetKind = AssetKind::StaticMesh,
        },
        SourceImportMetadataWriteOutput{
            .assetId = assetId(0x11U),
            .assetKind = AssetKind::Material,
        },
    };
    const std::array unitTwoOutputs{
        SourceImportMetadataWriteOutput{
            .assetId = assetId(0x20U),
            .assetKind = AssetKind::Texture2D,
        },
    };
    const std::array units{
        SourceImportMetadataWriteUnit{
            .unitId = unitId(1U),
            .importerKind = 7U,
            .importerVersion = 3U,
            .settingsHash = contentHash(0x21U),
            .inputs = unitOneInputs,
            .outputs = unitOneOutputs,
        },
        SourceImportMetadataWriteUnit{
            .unitId = unitId(2U),
            .importerKind = 8U,
            .importerVersion = 5U,
            .settingsHash = contentHash(0x22U),
            .inputs = unitTwoInputs,
            .outputs = unitTwoOutputs,
        },
    };
    auto result = writeSourceImportMetadataBytes(SourceImportMetadataWriteDesc{
        .targetPlatform = TargetPlatform::WindowsX64,
        .manifestRevision = SourceImportManifestRevision{
            .manifestDigest = contentHash(0x40U),
            .manifestBytes = 4096U,
        },
        .sources = sources,
        .units = units,
    });
    if (!result)
    {
        ADD_FAILURE() << result.error().message;
        return {};
    }
    return std::move(*result);
}

void expectFailure(Bytes bytes, const std::function<void(Bytes&)>& mutate, Core::ErrorCode expectedCode)
{
    mutate(bytes);
    const auto result = parseSourceImportMetadataView(bytes);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, expectedCode);
}

Core::Result<Bytes> writeSingleSource(std::string_view path,
                                      SourceImportReadExtent readExtent = SourceImportReadExtent::WholeFile)
{
    const std::array sources{SourceImportMetadataWriteSource{
        .path = path,
        .contentHash = contentHash(1U),
        .fileBytes = 1U,
        .readExtent = readExtent,
    }};
    const std::array inputs{SourceImportMetadataWriteInput{
        .sourceIndex = 0U,
        .flags = SourceImportInputFlags::Primary,
    }};
    const std::array outputs{SourceImportMetadataWriteOutput{
        .assetId = assetId(1U),
        .assetKind = AssetKind::Texture2D,
    }};
    const std::array units{SourceImportMetadataWriteUnit{
        .unitId = unitId(1U),
        .importerKind = 1U,
        .importerVersion = 1U,
        .settingsHash = contentHash(2U),
        .inputs = inputs,
        .outputs = outputs,
    }};
    return writeSourceImportMetadataBytes(SourceImportMetadataWriteDesc{
        .manifestRevision = SourceImportManifestRevision{
            .manifestDigest = contentHash(3U),
            .manifestBytes = 64U,
        },
        .sources = sources,
        .units = units,
    });
}

void expectPathWriteFailure(std::string_view path)
{
    const auto result = writeSingleSource(path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(SourceImportUnitIdTests, UsesStrictLowercaseCanonicalText)
{
    constexpr std::string_view Text = "00112233445566778899aabbccddeeff";
    const auto id = SourceImportUnitId::parseCanonical(Text);
    ASSERT_TRUE(id.has_value());
    const auto canonical = id->canonicalText();
    EXPECT_EQ(std::string_view(canonical.data(), canonical.size()), Text);
    EXPECT_FALSE(SourceImportUnitId::parseCanonical("00112233445566778899AABBCCDDEEFF"));
    EXPECT_FALSE(SourceImportUnitId::parseCanonical("0011"));
    EXPECT_FALSE(SourceImportUnitId::fromBytes({}));
}

TEST(SourceImportMetadataFormatTests, WritesAndParsesCanonicalMetadataDeterministically)
{
    const auto first = makeValidMetadata();
    const auto second = makeValidMetadata();
    ASSERT_FALSE(first.empty());
    EXPECT_EQ(first, second);

    const auto view = parseSourceImportMetadataView(first);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->header().schemaMajor, 1U);
    EXPECT_EQ(view->header().schemaMinor, 1U);
    EXPECT_EQ(view->header().sourceCount, 3U);
    EXPECT_EQ(view->header().unitCount, 2U);
    EXPECT_EQ(view->header().unitInputCount, 4U);
    EXPECT_EQ(view->header().outputCount, 3U);
    EXPECT_EQ(view->header().manifestRevision.manifestDigest, contentHash(0x40U));
    EXPECT_EQ(view->header().manifestRevision.manifestBytes, 4096U);
    EXPECT_EQ(view->sourcePath(0U), std::optional<std::string_view>{"characters/hero.glb"});
    EXPECT_EQ(view->sourcePath(2U), std::optional<std::string_view>{"textures/hero.png"});
    EXPECT_EQ(view->source(1U)->fileBytes, 256U);
    EXPECT_EQ(view->source(0U)->readExtent, SourceImportReadExtent::WholeFile);
    EXPECT_EQ(view->source(1U)->readExtent, SourceImportReadExtent::Prefix);
    EXPECT_EQ(view->unit(1U)->importerVersion, 5U);
    EXPECT_EQ(view->unitInputForUnit(1U, 0U)->sourceIndex, 0U);
    EXPECT_TRUE(hasSourceImportInputFlag(view->unitInputForUnit(1U, 1U)->flags,
                                        SourceImportInputFlags::Primary));
    EXPECT_EQ(view->outputForUnit(0U, 1U)->assetKind, AssetKind::Material);
    EXPECT_FALSE(view->source(3U));
    EXPECT_FALSE(view->outputForUnit(1U, 1U));
}

TEST(SourceImportMetadataFormatTests, ParsesCanonicalEmptyGraphBoundToManifestRevision)
{
    const auto bytes = writeSourceImportMetadataBytes(SourceImportMetadataWriteDesc{
        .targetPlatform = TargetPlatform::LinuxX64,
        .manifestRevision = SourceImportManifestRevision{
            .manifestDigest = contentHash(1U),
            .manifestBytes = 64U,
        },
    });
    ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
    const auto view = parseSourceImportMetadataView(*bytes);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->header().fileBytes, SourceImportWire::HeaderBytes);
    EXPECT_EQ(view->header().targetPlatform, TargetPlatform::LinuxX64);
}

TEST(SourceImportMetadataFormatTests, RejectsMagicOldSchemaFixedSizesAndReservedHeaderFields)
{
    const auto valid = makeValidMetadata();
    expectFailure(valid, [](Bytes& bytes) { putU8(bytes, 0U, 0U); }, AssetFormatErrorCode::InvalidMagic);
    expectFailure(valid, [](Bytes& bytes) { putU16(bytes, 10U, 0U); },
                  AssetFormatErrorCode::UnsupportedSchema);
    expectFailure(valid, [](Bytes& bytes) { putU32(bytes, 52U, 39U); }, AssetFormatErrorCode::InvalidHeader);
    expectFailure(valid, [](Bytes& bytes) { putU32(bytes, 20U, 1U); }, AssetFormatErrorCode::InvalidHeader);
    expectFailure(valid, [](Bytes& bytes) { putU64(bytes, 136U, 1U); }, AssetFormatErrorCode::InvalidHeader);
    expectFailure(
        valid,
        [](Bytes& bytes) {
            for (Core::usize index = 24U; index < 40U; ++index)
            {
                bytes[index] = std::byte{0};
            }
        },
        AssetFormatErrorCode::InvalidIdentity);
    expectFailure(valid, [](Bytes& bytes) { putU64(bytes, 40U, 0U); }, AssetFormatErrorCode::InvalidIdentity);
    expectFailure(valid, [](Bytes& bytes) { putU64(bytes, 40U, Wire::MaxManifestFileBytes + 1U); },
                  AssetFormatErrorCode::InvalidIdentity);
}

TEST(SourceImportMetadataFormatTests, RejectsCountOffsetSizeAndTrailingByteCorruption)
{
    const auto valid = makeValidMetadata();
    expectFailure(valid, [](Bytes& bytes) { putU32(bytes, 48U, SourceImportWire::MaxSources + 1U); },
                  AssetFormatErrorCode::SizeLimitExceeded);
    expectFailure(valid,
                  [](Bytes& bytes) { putU64(bytes, 112U, (std::numeric_limits<Core::u64>::max)()); },
                  AssetFormatErrorCode::SizeLimitExceeded);
    expectFailure(valid, [](Bytes& bytes) { putU64(bytes, 72U, SourceImportWire::HeaderBytes); },
                  AssetFormatErrorCode::InvalidLayout);
    expectFailure(valid, [](Bytes& bytes) { putU64(bytes, 128U, bytes.size() - 1U); },
                  AssetFormatErrorCode::InvalidLayout);
    expectFailure(valid, [](Bytes& bytes) { bytes.push_back(std::byte{0}); },
                  AssetFormatErrorCode::InvalidLayout);
}

TEST(SourceImportMetadataFormatTests, RejectsInvalidAndNonCanonicalPaths)
{
    const auto valid = makeValidMetadata();
    const auto view = parseSourceImportMetadataView(valid);
    ASSERT_TRUE(view.has_value());
    const auto firstPathOffset = static_cast<Core::usize>(view->source(0U)->pathOffset);

    expectFailure(valid, [firstPathOffset](Bytes& bytes) { bytes[firstPathOffset] = std::byte{'/'}; },
                  AssetFormatErrorCode::InvalidLayout);
    expectFailure(valid, [firstPathOffset](Bytes& bytes) { bytes[firstPathOffset] = std::byte{'\\'}; },
                  AssetFormatErrorCode::InvalidLayout);
    expectFailure(valid, [firstPathOffset](Bytes& bytes) { bytes[firstPathOffset] = std::byte{0}; },
                  AssetFormatErrorCode::InvalidLayout);
    expectFailure(valid, [firstPathOffset](Bytes& bytes) { bytes[firstPathOffset] = std::byte{0xC0}; },
                  AssetFormatErrorCode::InvalidLayout);
    expectFailure(valid, [](Bytes& bytes) { putU64(bytes, SourceImportWire::HeaderBytes + 24U, 0U); },
                  AssetFormatErrorCode::InvalidLayout);

    expectPathWriteFailure("../hero.glb");
    expectPathWriteFailure("folder//hero.glb");
    expectPathWriteFailure("C:/hero.glb");
    const std::array invalidUtf8{static_cast<char>(0xC0), static_cast<char>(0x80)};
    expectPathWriteFailure(std::string_view(invalidUtf8.data(), invalidUtf8.size()));
}

TEST(SourceImportMetadataFormatTests, RejectsInvalidRowsAndReservedBits)
{
    const auto valid = makeValidMetadata();
    const auto view = parseSourceImportMetadataView(valid);
    ASSERT_TRUE(view.has_value());
    const auto sourceOffset = static_cast<Core::usize>(view->header().sourcesOffset);
    const auto unitOffset = static_cast<Core::usize>(view->header().unitsOffset);
    const auto inputOffset = static_cast<Core::usize>(view->header().unitInputsOffset);
    const auto outputOffset = static_cast<Core::usize>(view->header().outputsOffset);

    expectFailure(valid, [sourceOffset](Bytes& bytes) { putU32(bytes, sourceOffset + 36U, 0U); },
                  AssetFormatErrorCode::UnsupportedValue);
    expectFailure(valid, [sourceOffset](Bytes& bytes) { putU32(bytes, sourceOffset + 36U, 3U); },
                  AssetFormatErrorCode::UnsupportedValue);
    expectFailure(valid, [unitOffset](Bytes& bytes) { putU32(bytes, unitOffset + 16U, 0U); },
                  AssetFormatErrorCode::UnsupportedValue);
    expectFailure(valid, [unitOffset](Bytes& bytes) { putU64(bytes, unitOffset + 56U, 1U); },
                  AssetFormatErrorCode::UnsupportedValue);
    expectFailure(valid, [inputOffset](Bytes& bytes) { putU16(bytes, inputOffset + 6U, 1U); },
                  AssetFormatErrorCode::InvalidDependency);
    expectFailure(valid, [outputOffset](Bytes& bytes) { putU16(bytes, outputOffset + 18U, 1U); },
                  AssetFormatErrorCode::InvalidIdentity);
    expectFailure(valid, [outputOffset](Bytes& bytes) { putU32(bytes, outputOffset + 20U, 1U); },
                  AssetFormatErrorCode::InvalidIdentity);
}

TEST(SourceImportMetadataFormatTests, RejectsUnitRangeInputAndSourceCoverageCorruption)
{
    const auto valid = makeValidMetadata();
    const auto view = parseSourceImportMetadataView(valid);
    ASSERT_TRUE(view.has_value());
    const auto firstUnitOffset = static_cast<Core::usize>(view->header().unitsOffset);
    const auto inputOffset = static_cast<Core::usize>(view->header().unitInputsOffset);

    expectFailure(valid, [firstUnitOffset](Bytes& bytes) { putU32(bytes, firstUnitOffset + 44U, 1U); },
                  AssetFormatErrorCode::InvalidLayout);
    expectFailure(valid, [inputOffset](Bytes& bytes) { putU16(bytes, inputOffset + 4U, 0U); },
                  AssetFormatErrorCode::InvalidDependency);
    expectFailure(valid, [inputOffset](Bytes& bytes) { putU16(bytes, inputOffset + 12U, 2U); },
                  AssetFormatErrorCode::InvalidDependency);
    expectFailure(valid, [inputOffset](Bytes& bytes) { putU32(bytes, inputOffset + 8U, 0U); },
                  AssetFormatErrorCode::InvalidDependency);
    expectFailure(valid, [inputOffset](Bytes& bytes) { putU32(bytes, inputOffset + 24U, 1U); },
                  AssetFormatErrorCode::InvalidDependency);
}

TEST(SourceImportMetadataFormatTests, RejectsUnitAndOutputDuplicatesOrSortViolations)
{
    const auto valid = makeValidMetadata();
    const auto view = parseSourceImportMetadataView(valid);
    ASSERT_TRUE(view.has_value());
    const auto secondUnitOffset = static_cast<Core::usize>(view->header().unitsOffset) +
                                  SourceImportWire::UnitEntryBytes;
    const auto outputOffset = static_cast<Core::usize>(view->header().outputsOffset);

    expectFailure(valid,
                  [secondUnitOffset](Bytes& bytes) { putFixed(bytes, secondUnitOffset, unitIdBytes(1U)); },
                  AssetFormatErrorCode::InvalidIdentity);
    expectFailure(valid,
                  [outputOffset](Bytes& bytes) {
                      putFixed(bytes, outputOffset + SourceImportWire::OutputEntryBytes, assetIdBytes(9U));
                  },
                  AssetFormatErrorCode::InvalidIdentity);
    expectFailure(valid,
                  [outputOffset](Bytes& bytes) {
                      putFixed(bytes, outputOffset + 2U * SourceImportWire::OutputEntryBytes, assetIdBytes(0x10U));
                  },
                  AssetFormatErrorCode::InvalidIdentity);
}

TEST(SourceImportMetadataFormatTests, WriterRejectsUnsortedAndDuplicateSourceRows)
{
    const std::array inputs{SourceImportMetadataWriteInput{
        .sourceIndex = 0U,
        .flags = SourceImportInputFlags::Primary,
    }};
    const std::array outputs{SourceImportMetadataWriteOutput{
        .assetId = assetId(1U),
        .assetKind = AssetKind::Texture2D,
    }};
    const std::array units{SourceImportMetadataWriteUnit{
        .unitId = unitId(1U),
        .importerKind = 1U,
        .importerVersion = 1U,
        .settingsHash = contentHash(1U),
        .inputs = inputs,
        .outputs = outputs,
    }};
    const std::array unsortedSources{
        SourceImportMetadataWriteSource{.path = "z.png",
                                        .contentHash = contentHash(2U),
                                        .fileBytes = 1U,
                                        .readExtent = SourceImportReadExtent::WholeFile},
        SourceImportMetadataWriteSource{.path = "a.png",
                                        .contentHash = contentHash(3U),
                                        .fileBytes = 1U,
                                        .readExtent = SourceImportReadExtent::WholeFile},
    };
    const auto result = writeSourceImportMetadataBytes(SourceImportMetadataWriteDesc{
        .manifestRevision = SourceImportManifestRevision{
            .manifestDigest = contentHash(4U),
            .manifestBytes = 64U,
        },
        .sources = unsortedSources,
        .units = units,
    });
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetFormatErrorCode::InvalidLayout);

    const std::array duplicateSources{
        SourceImportMetadataWriteSource{.path = "a.png",
                                        .contentHash = contentHash(2U),
                                        .fileBytes = 1U,
                                        .readExtent = SourceImportReadExtent::WholeFile},
        SourceImportMetadataWriteSource{.path = "a.png",
                                        .contentHash = contentHash(3U),
                                        .fileBytes = 1U,
                                        .readExtent = SourceImportReadExtent::WholeFile},
    };
    const auto duplicateResult = writeSourceImportMetadataBytes(SourceImportMetadataWriteDesc{
        .manifestRevision = SourceImportManifestRevision{
            .manifestDigest = contentHash(4U),
            .manifestBytes = 64U,
        },
        .sources = duplicateSources,
        .units = units,
    });
    ASSERT_FALSE(duplicateResult.has_value());
    EXPECT_EQ(duplicateResult.error().code, AssetFormatErrorCode::InvalidLayout);
}

TEST(SourceImportMetadataFormatTests, WriterRejectsInvalidAndUnknownReadExtents)
{
    auto result = writeSingleSource("texture.png", SourceImportReadExtent::Invalid);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetFormatErrorCode::UnsupportedValue);

    result = writeSingleSource("texture.png", static_cast<SourceImportReadExtent>(3U));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetFormatErrorCode::UnsupportedValue);
}

TEST(SourceImportMetadataFormatTests, EnforcesCallerLimitsWithoutExpandingHardLimits)
{
    const auto valid = makeValidMetadata();
    SourceImportMetadataLimits limits{};
    limits.maxSources = 2U;
    auto result = parseSourceImportMetadataView(valid, limits);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetFormatErrorCode::SizeLimitExceeded);

    limits = {};
    limits.maxOutputs = SourceImportWire::MaxOutputs + 1U;
    result = parseSourceImportMetadataView(valid, limits);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, AssetFormatErrorCode::InvalidLimits);
}

} // namespace
} // namespace Tina::AssetFormat
