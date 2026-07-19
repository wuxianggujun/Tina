#include <tina/asset_format/AssetFormat.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/hash/ContentHashDigest.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::AssetFormat {
namespace {

using Bytes = std::vector<std::byte>;

struct DependencySpec final {
    Core::u8 idSeed = 0;
    AssetKind expectedKind = AssetKind::Invalid;
};

struct ManifestEntrySpec final {
    Core::u8 idSeed = 0;
    AssetKind kind = AssetKind::Invalid;
    std::vector<DependencySpec> dependencies;
};

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

template <Core::usize Size> void putFixed(Bytes& bytes, Core::usize offset, const std::array<std::byte, Size>& value)
{
    std::copy(value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

Core::AssetId::Bytes idBytes(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return bytes;
}

Core::ContentHash::Bytes hashBytes(Core::u8 seed)
{
    Core::ContentHash::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[7] = static_cast<std::byte>(seed ^ 0xA5U);
    return bytes;
}

Core::u64 alignUp(Core::u64 value, Core::u32 alignment)
{
    return (value + alignment - 1U) & ~(static_cast<Core::u64>(alignment) - 1U);
}

Bytes makeCookedAsset(std::span<const DependencySpec> dependencies = {}, Core::u32 payloadAlignment = 16U)
{
    constexpr std::array<std::byte, 4> Payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto dependencyEnd = Wire::CookedAssetHeaderBytes + dependencies.size() * Wire::DependencyEntryBytes;
    const auto payloadOffset = alignUp(dependencyEnd, payloadAlignment);
    const auto fileBytes = payloadOffset + Payload.size();
    Bytes bytes(static_cast<Core::usize>(fileBytes), std::byte{0});

    putFixed(bytes, 0U, Wire::CookedAssetMagic);
    putU16(bytes, 8U, Wire::SchemaMajor);
    putU16(bytes, 10U, Wire::SchemaMinor);
    putU32(bytes, 12U, Wire::CookedAssetHeaderBytes);
    putU16(bytes, 16U, static_cast<Core::u16>(AssetKind::Sprite));
    putU16(bytes, 18U, 1U);
    putU16(bytes, 20U, static_cast<Core::u16>(TargetPlatform::WindowsX64));
    putU8(bytes, 22U, static_cast<Core::u8>(EndianTag::Little));
    putU8(bytes, 23U, static_cast<Core::u8>(HashAlgorithm::Xxh3_128V1));
    putFixed(bytes, 32U, idBytes(0x40U));
    putFixed(bytes, 48U, hashBytes(0x50U));
    putU64(bytes, 64U, Wire::CookedAssetHeaderBytes);
    putU32(bytes, 72U, static_cast<Core::u32>(dependencies.size()));
    putU32(bytes, 76U, Wire::DependencyEntryBytes);
    putU64(bytes, 80U, payloadOffset);
    putU64(bytes, 88U, Payload.size());
    putU32(bytes, 96U, payloadAlignment);
    putU64(bytes, 104U, fileBytes);

    for (Core::usize index = 0; index < dependencies.size(); ++index)
    {
        const auto offset = Wire::CookedAssetHeaderBytes + index * Wire::DependencyEntryBytes;
        putFixed(bytes, offset, idBytes(dependencies[index].idSeed));
        putU16(bytes, offset + 16U, static_cast<Core::u16>(dependencies[index].expectedKind));
        putU16(bytes, offset + 18U, static_cast<Core::u16>(DependencyFlags::Required));
    }
    putFixed(bytes, static_cast<Core::usize>(payloadOffset), Payload);
    return bytes;
}

Bytes makeManifest(std::span<const ManifestEntrySpec> entries)
{
    Core::u32 dependencyCount = 0;
    for (const auto& entry : entries)
    {
        dependencyCount += static_cast<Core::u32>(entry.dependencies.size());
    }
    const auto dependencyOffset = Wire::CookedManifestHeaderBytes + entries.size() * Wire::ManifestEntryBytes;
    const auto fileBytes = dependencyOffset + static_cast<Core::u64>(dependencyCount) * Wire::DependencyEntryBytes;
    Bytes bytes(static_cast<Core::usize>(fileBytes), std::byte{0});

    putFixed(bytes, 0U, Wire::CookedManifestMagic);
    putU16(bytes, 8U, Wire::SchemaMajor);
    putU16(bytes, 10U, Wire::SchemaMinor);
    putU32(bytes, 12U, Wire::CookedManifestHeaderBytes);
    putU16(bytes, 16U, static_cast<Core::u16>(TargetPlatform::WindowsX64));
    putU8(bytes, 18U, static_cast<Core::u8>(EndianTag::Little));
    putU8(bytes, 19U, static_cast<Core::u8>(HashAlgorithm::Xxh3_128V1));
    putU32(bytes, 24U, static_cast<Core::u32>(entries.size()));
    putU32(bytes, 28U, Wire::ManifestEntryBytes);
    putU32(bytes, 32U, dependencyCount);
    putU32(bytes, 36U, Wire::DependencyEntryBytes);
    putU64(bytes, 40U, Wire::CookedManifestHeaderBytes);
    putU64(bytes, 48U, dependencyOffset);
    putU64(bytes, 56U, fileBytes);

    Core::u32 dependencyFirst = 0;
    for (Core::usize entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
    {
        const auto& entry = entries[entryIndex];
        const auto offset = Wire::CookedManifestHeaderBytes + entryIndex * Wire::ManifestEntryBytes;
        putFixed(bytes, offset, idBytes(entry.idSeed));
        putFixed(bytes, offset + 16U, hashBytes(static_cast<Core::u8>(entry.idSeed + 0x20U)));
        putU16(bytes, offset + 32U, static_cast<Core::u16>(entry.kind));
        putU16(bytes, offset + 34U, 1U);
        putU32(bytes, offset + 40U, dependencyFirst);
        putU32(bytes, offset + 44U, static_cast<Core::u32>(entry.dependencies.size()));
        putU64(bytes, offset + 48U, 256U + entry.idSeed);

        for (Core::usize localIndex = 0; localIndex < entry.dependencies.size(); ++localIndex)
        {
            const auto dependencyIndex = dependencyFirst + static_cast<Core::u32>(localIndex);
            const auto dependencyEntryOffset = dependencyOffset + dependencyIndex * Wire::DependencyEntryBytes;
            putFixed(bytes, dependencyEntryOffset, idBytes(entry.dependencies[localIndex].idSeed));
            putU16(bytes, dependencyEntryOffset + 16U,
                   static_cast<Core::u16>(entry.dependencies[localIndex].expectedKind));
            putU16(bytes, dependencyEntryOffset + 18U, static_cast<Core::u16>(DependencyFlags::Required));
        }
        dependencyFirst += static_cast<Core::u32>(entry.dependencies.size());
    }
    return bytes;
}

std::vector<ManifestEntrySpec> validManifestSpecs()
{
    return {
        ManifestEntrySpec{.idSeed = 1U, .kind = AssetKind::Texture2D},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetKind::Material,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetKind::Texture2D}}},
        ManifestEntrySpec{.idSeed = 3U,
                          .kind = AssetKind::Prefab,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetKind::Texture2D},
                                           {.idSeed = 2U, .expectedKind = AssetKind::Material}}},
    };
}

void expectCookedFailure(Bytes bytes, const std::function<void(Bytes&)>& mutate, Core::ErrorCode expectedCode)
{
    mutate(bytes);
    const auto result = parseCookedAssetView(bytes);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, expectedCode);
}

void expectManifestFailure(Bytes bytes, const std::function<void(Bytes&)>& mutate, Core::ErrorCode expectedCode)
{
    mutate(bytes);
    const auto result = parseCookedManifestView(bytes);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, expectedCode);
}

TEST(AssetIdentityTests, AssetIdUsesStrictLowercaseCanonicalText)
{
    constexpr std::string_view Text = "00112233445566778899aabbccddeeff";
    const auto assetId = Core::AssetId::parseCanonical(Text);
    ASSERT_TRUE(assetId.has_value());
    const auto canonical = assetId->canonicalText();
    EXPECT_EQ(std::string_view(canonical.data(), canonical.size()), Text);
    EXPECT_FALSE(Core::AssetId::parseCanonical("00112233445566778899AABBCCDDEEFF"));
    EXPECT_FALSE(Core::AssetId::parseCanonical("0011"));
    EXPECT_FALSE(Core::AssetId::parseCanonical("00112233445566778899aabbccddeefg"));
    EXPECT_FALSE(Core::AssetId::parseCanonical("00000000000000000000000000000000"));
}

TEST(AssetIdentityTests, ContentHashRejectsZeroWithoutBecomingAssetIdentity)
{
    EXPECT_FALSE(Core::ContentHash::fromBytes({}));
    const auto contentHash = Core::ContentHash::fromBytes(hashBytes(1U));
    ASSERT_TRUE(contentHash.has_value());
    EXPECT_TRUE(contentHash->hasValue());
}

TEST(CookedAssetFormatTests, ParsesCanonicalAssetAndBorrowsPayload)
{
    const std::array dependencies{DependencySpec{.idSeed = 1U, .expectedKind = AssetKind::Texture2D},
                                  DependencySpec{.idSeed = 2U, .expectedKind = AssetKind::Material}};
    auto bytes = makeCookedAsset(dependencies, 64U);
    const auto result = parseCookedAssetView(bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().assetKind, AssetKind::Sprite);
    EXPECT_EQ(result->header().dependencyCount, 2U);
    ASSERT_TRUE(result->dependency(1U));
    EXPECT_EQ(result->dependency(1U)->expectedKind, AssetKind::Material);
    EXPECT_FALSE(result->dependency(2U));
    ASSERT_EQ(result->payload().size(), 4U);
    EXPECT_EQ(result->payload()[0], std::byte{0x10});
}

TEST(CookedAssetFormatTests, VerifiesMatchingPayloadContentHash)
{
    auto bytes = makeCookedAsset();
    auto parsed = parseCookedAssetView(bytes);
    ASSERT_TRUE(parsed.has_value());

    const auto digest = Core::digestContentHashV1(parsed->payload());
    ASSERT_TRUE(digest.has_value()) << digest.error().message;
    putFixed(bytes, 48U, digest->bytes());

    parsed = parseCookedAssetView(bytes);
    ASSERT_TRUE(parsed.has_value());
    const auto status = verifyCookedAssetContentHash(*parsed);
    ASSERT_TRUE(status.has_value()) << status.error().message;
}

TEST(CookedAssetFormatTests, RejectsMismatchedPayloadContentHash)
{
    auto bytes = makeCookedAsset();
    // keep non-zero but wrong content hash already present from fixture
    const auto parsed = parseCookedAssetView(bytes);
    ASSERT_TRUE(parsed.has_value());
    const auto status = verifyCookedAssetContentHash(*parsed);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, AssetFormatErrorCode::ContentHashMismatch);
}

TEST(CookedAssetFormatTests, DerivesCanonicalArtifactPathFromKindAndIdentity)
{
    const auto assetId = Core::AssetId::fromBytes(idBytes(0xABU));
    ASSERT_TRUE(assetId);
    const auto path = makeCookedArtifactPath(AssetKind::Sprite, *assetId);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->view(), "objects/0004/ab/ab0000000000000000000000000000f1.tasset");
    EXPECT_EQ(path->storage.back(), '\0');
}

TEST(CookedAssetFormatTests, RejectsHeaderIdentityAndEnumCorruption)
{
    const auto valid = makeCookedAsset();
    expectCookedFailure(valid, [](Bytes& value) { putU8(value, 0U, 0U); }, AssetFormatErrorCode::InvalidMagic);
    expectCookedFailure(valid, [](Bytes& value) { putU16(value, 8U, 2U); }, AssetFormatErrorCode::UnsupportedSchema);
    expectCookedFailure(valid, [](Bytes& value) { putU32(value, 12U, 111U); }, AssetFormatErrorCode::InvalidHeader);
    expectCookedFailure(valid, [](Bytes& value) { putU8(value, 22U, 2U); }, AssetFormatErrorCode::UnsupportedValue);
    expectCookedFailure(valid, [](Bytes& value) { putU8(value, 23U, 9U); }, AssetFormatErrorCode::UnsupportedValue);
    expectCookedFailure(valid, [](Bytes& value) { putU16(value, 16U, 99U); }, AssetFormatErrorCode::UnsupportedValue);
    expectCookedFailure(valid, [](Bytes& value) { putU16(value, 18U, 0U); }, AssetFormatErrorCode::InvalidHeader);
    expectCookedFailure(valid, [](Bytes& value) { putU32(value, 24U, 1U); }, AssetFormatErrorCode::InvalidHeader);
    expectCookedFailure(
        valid,
        [](Bytes& value) {
            putU8(value, 32U, 0U);
            putU8(value, 47U, 0U);
        },
        AssetFormatErrorCode::InvalidIdentity);
    expectCookedFailure(
        valid,
        [](Bytes& value) {
            putU8(value, 48U, 0U);
            putU8(value, 55U, 0U);
        },
        AssetFormatErrorCode::InvalidIdentity);
}

TEST(CookedAssetFormatTests, RejectsTruncationOverflowAndNonCanonicalLayout)
{
    const auto valid = makeCookedAsset({}, 64U);
    expectCookedFailure(valid, [](Bytes& value) { value.resize(20U); }, AssetFormatErrorCode::InvalidHeader);
    expectCookedFailure(
        valid,
        [](Bytes& value) {
            putU64(value, 80U, (std::numeric_limits<Core::u64>::max)() - 1U);
            putU64(value, 88U, 8U);
        },
        AssetFormatErrorCode::ArithmeticOverflow);
    expectCookedFailure(valid, [](Bytes& value) { putU64(value, 64U, 0U); }, AssetFormatErrorCode::InvalidLayout);
    expectCookedFailure(
        valid, [](Bytes& value) { putU64(value, 104U, value.size() - 1U); }, AssetFormatErrorCode::InvalidLayout);
    expectCookedFailure(
        valid, [](Bytes& value) { value.push_back(std::byte{0}); }, AssetFormatErrorCode::InvalidLayout);
    expectCookedFailure(valid, [](Bytes& value) { putU32(value, 96U, 0U); }, AssetFormatErrorCode::InvalidLayout);
    expectCookedFailure(valid, [](Bytes& value) { putU32(value, 96U, 3U); }, AssetFormatErrorCode::InvalidLayout);
    expectCookedFailure(valid, [](Bytes& value) { putU32(value, 96U, 8192U); }, AssetFormatErrorCode::InvalidLayout);
    expectCookedFailure(valid, [](Bytes& value) { putU8(value, 112U, 1U); }, AssetFormatErrorCode::InvalidLayout);
    expectCookedFailure(valid, [](Bytes& value) { putU64(value, 88U, 0U); }, AssetFormatErrorCode::SizeLimitExceeded);
}

TEST(CookedAssetFormatTests, RejectsInvalidDependenciesAndLimits)
{
    const std::array dependencies{DependencySpec{.idSeed = 1U, .expectedKind = AssetKind::Texture2D},
                                  DependencySpec{.idSeed = 2U, .expectedKind = AssetKind::Material}};
    const auto valid = makeCookedAsset(dependencies);
    const auto secondOffset = Wire::CookedAssetHeaderBytes + Wire::DependencyEntryBytes;
    expectCookedFailure(
        valid, [secondOffset](Bytes& value) { putFixed(value, secondOffset, idBytes(1U)); },
        AssetFormatErrorCode::InvalidDependency);
    expectCookedFailure(
        valid, [](Bytes& value) { putFixed(value, Wire::CookedAssetHeaderBytes, idBytes(0x40U)); },
        AssetFormatErrorCode::InvalidDependency);
    expectCookedFailure(
        valid, [](Bytes& value) { putU16(value, Wire::CookedAssetHeaderBytes + 16U, 99U); },
        AssetFormatErrorCode::InvalidDependency);
    expectCookedFailure(
        valid, [](Bytes& value) { putU16(value, Wire::CookedAssetHeaderBytes + 18U, 0U); },
        AssetFormatErrorCode::InvalidDependency);
    expectCookedFailure(
        valid, [](Bytes& value) { putU32(value, Wire::CookedAssetHeaderBytes + 20U, 1U); },
        AssetFormatErrorCode::InvalidDependency);

    CookedAssetLimits limits{};
    limits.maxDependencies = 1U;
    const auto tooMany = parseCookedAssetView(valid, limits);
    ASSERT_FALSE(tooMany);
    EXPECT_EQ(tooMany.error().code, AssetFormatErrorCode::SizeLimitExceeded);
    limits = {};
    limits.maxPayloadBytes = 2U;
    const auto payloadTooLarge = parseCookedAssetView(valid, limits);
    ASSERT_FALSE(payloadTooLarge);
    EXPECT_EQ(payloadTooLarge.error().code, AssetFormatErrorCode::SizeLimitExceeded);
}

TEST(CookedManifestFormatTests, ParsesEmptyAndPopulatedCanonicalManifests)
{
    const auto empty = makeManifest({});
    const auto emptyResult = parseCookedManifestView(empty);
    ASSERT_TRUE(emptyResult.has_value());
    EXPECT_EQ(emptyResult->header().entryCount, 0U);

    const auto specs = validManifestSpecs();
    auto bytes = makeManifest(specs);
    const auto result = parseCookedManifestView(bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().entryCount, 3U);
    EXPECT_EQ(result->header().dependencyCount, 3U);
    ASSERT_TRUE(result->entry(2U));
    EXPECT_EQ(result->entry(2U)->assetKind, AssetKind::Prefab);
    ASSERT_TRUE(result->dependencyForEntry(2U, 1U));
    EXPECT_EQ(result->dependencyForEntry(2U, 1U)->expectedKind, AssetKind::Material);
    EXPECT_FALSE(result->dependencyForEntry(0U, 0U));
}

TEST(CookedManifestFormatTests, RejectsHeaderAndLayoutCorruption)
{
    const auto valid = makeManifest(validManifestSpecs());
    expectManifestFailure(valid, [](Bytes& value) { putU8(value, 0U, 0U); }, AssetFormatErrorCode::InvalidMagic);
    expectManifestFailure(valid, [](Bytes& value) { putU16(value, 10U, 1U); }, AssetFormatErrorCode::UnsupportedSchema);
    expectManifestFailure(valid, [](Bytes& value) { putU32(value, 12U, 63U); }, AssetFormatErrorCode::InvalidHeader);
    expectManifestFailure(valid, [](Bytes& value) { putU32(value, 28U, 55U); }, AssetFormatErrorCode::InvalidHeader);
    expectManifestFailure(valid, [](Bytes& value) { putU8(value, 18U, 2U); }, AssetFormatErrorCode::UnsupportedValue);
    expectManifestFailure(valid, [](Bytes& value) { putU32(value, 20U, 1U); }, AssetFormatErrorCode::InvalidHeader);
    expectManifestFailure(valid, [](Bytes& value) { putU64(value, 40U, 0U); }, AssetFormatErrorCode::InvalidLayout);
    expectManifestFailure(valid, [](Bytes& value) { putU64(value, 48U, 64U); }, AssetFormatErrorCode::InvalidLayout);
    expectManifestFailure(
        valid, [](Bytes& value) { value.push_back(std::byte{0}); }, AssetFormatErrorCode::InvalidLayout);
}

TEST(CookedManifestFormatTests, RejectsInvalidEntriesAndDependencyRanges)
{
    const auto valid = makeManifest(validManifestSpecs());
    const auto firstEntry = Wire::CookedManifestHeaderBytes;
    const auto secondEntry = firstEntry + Wire::ManifestEntryBytes;
    expectManifestFailure(
        valid,
        [firstEntry](Bytes& value) {
            putU8(value, firstEntry, 0U);
            putU8(value, firstEntry + 15U, 0U);
        },
        AssetFormatErrorCode::InvalidIdentity);
    expectManifestFailure(
        valid,
        [firstEntry](Bytes& value) {
            putU8(value, firstEntry + 16U, 0U);
            putU8(value, firstEntry + 23U, 0U);
        },
        AssetFormatErrorCode::InvalidIdentity);
    expectManifestFailure(
        valid, [firstEntry](Bytes& value) { putU16(value, firstEntry + 32U, 99U); },
        AssetFormatErrorCode::UnsupportedValue);
    expectManifestFailure(
        valid, [firstEntry](Bytes& value) { putU16(value, firstEntry + 34U, 0U); },
        AssetFormatErrorCode::UnsupportedValue);
    expectManifestFailure(
        valid, [firstEntry](Bytes& value) { putU32(value, firstEntry + 36U, 1U); },
        AssetFormatErrorCode::UnsupportedValue);
    expectManifestFailure(
        valid, [firstEntry](Bytes& value) { putU64(value, firstEntry + 48U, 0U); },
        AssetFormatErrorCode::SizeLimitExceeded);
    expectManifestFailure(
        valid, [firstEntry, secondEntry](Bytes& value) { putFixed(value, secondEntry, idBytes(1U)); },
        AssetFormatErrorCode::InvalidIdentity);
    expectManifestFailure(
        valid, [secondEntry](Bytes& value) { putU32(value, secondEntry + 40U, 1U); },
        AssetFormatErrorCode::InvalidDependency);
    expectManifestFailure(
        valid, [secondEntry](Bytes& value) { putU32(value, secondEntry + 44U, 0U); },
        AssetFormatErrorCode::InvalidDependency);
}

TEST(CookedManifestFormatTests, RejectsMissingSelfMismatchedAndUnsortedDependencies)
{
    auto specs = validManifestSpecs();
    auto valid = makeManifest(specs);
    const auto dependencyOffset = Wire::CookedManifestHeaderBytes + specs.size() * Wire::ManifestEntryBytes;
    expectManifestFailure(
        valid, [dependencyOffset](Bytes& value) { putFixed(value, dependencyOffset, idBytes(9U)); },
        AssetFormatErrorCode::MissingDependency);
    expectManifestFailure(
        valid, [dependencyOffset](Bytes& value) { putFixed(value, dependencyOffset, idBytes(2U)); },
        AssetFormatErrorCode::InvalidDependency);
    expectManifestFailure(
        valid,
        [dependencyOffset](Bytes& value) {
            putU16(value, dependencyOffset + 16U, static_cast<Core::u16>(AssetKind::Material));
        },
        AssetFormatErrorCode::DependencyTypeMismatch);
    expectManifestFailure(
        valid, [dependencyOffset](Bytes& value) { putU16(value, dependencyOffset + 18U, 0U); },
        AssetFormatErrorCode::InvalidDependency);
    expectManifestFailure(
        valid, [dependencyOffset](Bytes& value) { putU32(value, dependencyOffset + 20U, 1U); },
        AssetFormatErrorCode::InvalidDependency);

    const auto thirdEntryDependencies = dependencyOffset + Wire::DependencyEntryBytes;
    expectManifestFailure(
        valid,
        [thirdEntryDependencies](Bytes& value) {
            putFixed(value, thirdEntryDependencies + Wire::DependencyEntryBytes, idBytes(1U));
        },
        AssetFormatErrorCode::InvalidDependency);
}

TEST(CookedManifestFormatTests, DocumentsThatA0DoesNotRejectDependencyCycles)
{
    const std::vector specs{
        ManifestEntrySpec{.idSeed = 1U,
                          .kind = AssetKind::Material,
                          .dependencies = {{.idSeed = 2U, .expectedKind = AssetKind::Prefab}}},
        ManifestEntrySpec{.idSeed = 2U,
                          .kind = AssetKind::Prefab,
                          .dependencies = {{.idSeed = 1U, .expectedKind = AssetKind::Material}}},
    };
    const auto result = parseCookedManifestView(makeManifest(specs));
    EXPECT_TRUE(result.has_value());
}

TEST(CookedManifestFormatTests, EnforcesCallerLimitsWithoutAllowingHardLimitExpansion)
{
    const auto bytes = makeManifest(validManifestSpecs());
    CookedManifestLimits limits{};
    limits.maxEntries = 2U;
    auto result = parseCookedManifestView(bytes, limits);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, AssetFormatErrorCode::SizeLimitExceeded);

    limits = {};
    limits.maxEntries = Wire::MaxManifestEntries + 1U;
    result = parseCookedManifestView(bytes, limits);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, AssetFormatErrorCode::InvalidLimits);
}

TEST(AssetFormatStabilityTests, RepeatedBorrowedParsingHasNoStateDrift)
{
    const auto cooked = makeCookedAsset();
    const auto manifest = makeManifest(validManifestSpecs());
    for (int iteration = 0; iteration < 300; ++iteration)
    {
        const auto cookedResult = parseCookedAssetView(cooked);
        const auto manifestResult = parseCookedManifestView(manifest);
        ASSERT_TRUE(cookedResult.has_value());
        ASSERT_TRUE(manifestResult.has_value());
        EXPECT_EQ(cookedResult->payload().size(), 4U);
        EXPECT_EQ(manifestResult->header().entryCount, 3U);
    }
}

} // namespace
} // namespace Tina::AssetFormat
