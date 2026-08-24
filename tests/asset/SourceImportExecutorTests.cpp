#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/SourceImportExecutor.hpp>

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] Core::ContentHash hash(Core::u8 seed)
{
    Core::ContentHash::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0xA5U);
    return *Core::ContentHash::fromBytes(bytes);
}

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return *Core::AssetId::fromBytes(bytes);
}

[[nodiscard]] AssetFormat::SourceImportUnitId unitId(Core::u8 seed)
{
    AssetFormat::SourceImportUnitId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x3CU);
    return *AssetFormat::SourceImportUnitId::fromBytes(bytes);
}

[[nodiscard]] SourceImportCandidate makeCandidate(Core::u8 unitSeed,
                                                  Core::u8 outputSeed,
                                                  std::string path,
                                                  SourceImporterKind importerKind =
                                                      SourceImporterKind::CatalogRecipe)
{
    SourceImportCandidate candidate{};
    candidate.sources.push_back(SourceImportCapturedSource{
        .path = std::move(path),
        .contentHash = hash(unitSeed),
        .fileBytes = 64U,
        .readExtent = AssetFormat::SourceImportReadExtent::WholeFile,
    });
    candidate.units.push_back(SourceImportCapturedUnit{
        .unitId = unitId(unitSeed),
        .importerKind = importerKind,
        .importerVersion = 1U,
        .settingsHash = hash(static_cast<Core::u8>(unitSeed + 10U)),
        .inputs = {{.sourceIndex = 0U, .flags = AssetFormat::SourceImportInputFlags::Primary}},
        .outputs = {{.assetId = assetId(outputSeed),
                     .assetKind = AssetFormat::AssetKind::Texture2D}},
    });
    return candidate;
}

TEST(SourceImportExecutorTests, ComposesRetainedAndRecookedUnits)
{
    const auto first = makeCandidate(1U, 1U, "first.recipe");
    const auto second = makeCandidate(2U, 2U, "second.recipe");
    const std::array baselineParts{first, second};
    auto baselineCandidate = composeSourceImportCandidate(
        SourceImportCandidateComposeDesc{.recookedCandidates = baselineParts});
    ASSERT_TRUE(baselineCandidate.has_value()) << baselineCandidate.error().message;

    auto bytes = writeSourceImportCandidateBytes(
        baselineCandidate->candidate,
        AssetFormat::SourceImportManifestRevision{.manifestDigest = hash(90U),
                                                   .manifestBytes = 128U});
    ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
    auto baseline = AssetFormat::parseSourceImportMetadataView(*bytes);
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;

    const auto recooked = makeCandidate(3U, 3U, "third.recipe");
    const std::array retained{unitId(1U)};
    const std::array recookedParts{recooked};
    auto composed = composeSourceImportCandidate(SourceImportCandidateComposeDesc{
        .baseline = &*baseline,
        .retainedUnitIds = retained,
        .recookedCandidates = recookedParts,
    });
    ASSERT_TRUE(composed.has_value()) << composed.error().message;
    EXPECT_EQ(composed->candidate.units.size(), 2U);
    EXPECT_EQ(composed->candidate.sources.size(), 2U);
    ASSERT_EQ(composed->retainedAssetIds.size(), 1U);
    EXPECT_EQ(composed->retainedAssetIds[0], assetId(1U));
}

TEST(SourceImportExecutorTests, RejectsDuplicateOutputOwners)
{
    const auto first = makeCandidate(1U, 1U, "first.recipe");
    const auto second = makeCandidate(2U, 1U, "second.recipe");
    const std::array candidates{first, second};
    auto composed = composeSourceImportCandidate(
        SourceImportCandidateComposeDesc{.recookedCandidates = candidates});
    ASSERT_FALSE(composed.has_value());
    EXPECT_EQ(composed.error().code, AssetErrorCode::InvalidCatalogConfig);
}

TEST(SourceImportExecutorTests, RetainsTextureAndAudioUnits)
{
    const auto texture = makeCandidate(1U, 1U, "image.png", SourceImporterKind::Texture);
    const auto audio = makeCandidate(2U, 2U, "sound.wav", SourceImporterKind::Audio);
    const std::array baselineParts{texture, audio};
    auto baselineCandidate = composeSourceImportCandidate(
        SourceImportCandidateComposeDesc{.recookedCandidates = baselineParts});
    ASSERT_TRUE(baselineCandidate.has_value()) << baselineCandidate.error().message;

    auto bytes = writeSourceImportCandidateBytes(
        baselineCandidate->candidate,
        AssetFormat::SourceImportManifestRevision{.manifestDigest = hash(90U),
                                                   .manifestBytes = 128U});
    ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
    auto baseline = AssetFormat::parseSourceImportMetadataView(*bytes);
    ASSERT_TRUE(baseline.has_value()) << baseline.error().message;

    const std::array retained{unitId(1U), unitId(2U)};
    auto composed = composeSourceImportCandidate(SourceImportCandidateComposeDesc{
        .baseline = &*baseline,
        .retainedUnitIds = retained,
    });
    ASSERT_TRUE(composed.has_value()) << composed.error().message;
    ASSERT_EQ(composed->candidate.units.size(), 2U);
    EXPECT_EQ(composed->candidate.units[0].importerKind, SourceImporterKind::Texture);
    EXPECT_EQ(composed->candidate.units[1].importerKind, SourceImporterKind::Audio);
    EXPECT_EQ(composed->retainedAssetIds.size(), 2U);
}

} // namespace
} // namespace Tina::Asset
