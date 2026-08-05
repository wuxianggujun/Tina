#include <tina/asset/SourceImportCapture.hpp>

#include <tina/core/hash/ContentHashDigest.hpp>
#include <tina/core/io/ReadFile.hpp>
#include <tina/core/io/WriteFile.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>

namespace Tina::Asset {
namespace {

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}

[[nodiscard]] Core::AssetId assetId(Core::u8 seed)
{
    Core::AssetId::Bytes bytes{};
    bytes[0] = static_cast<std::byte>(seed);
    bytes[15] = static_cast<std::byte>(seed ^ 0x5AU);
    return *Core::AssetId::fromBytes(bytes);
}

TEST(SourceImportCaptureTests, CapturesCallerBytesAndWritesCanonicalMetadata)
{
    const auto root = std::filesystem::temp_directory_path() / "tina_source_import_capture";
    const std::string rootUtf8 = toUtf8(root);
    const SourceImportCaptureConfig config{.sourceRootUtf8 = rootUtf8};
    constexpr std::array RecipeBytes{std::byte{'r'}, std::byte{'e'}, std::byte{'c'}};
    constexpr std::array PayloadBytes{std::byte{'p'}, std::byte{'a'}, std::byte{'y'}};

    SourceImportCandidate candidate{};
    auto payloadIndex = captureSourceImportBytes(candidate, config, toUtf8(root / "payload.bin"),
                                                 AssetFormat::SourceImportReadExtent::Prefix, PayloadBytes);
    auto recipeIndex = captureSourceImportBytes(candidate, config, toUtf8(root / "recipes/main.recipe"),
                                                AssetFormat::SourceImportReadExtent::WholeFile, RecipeBytes);
    ASSERT_TRUE(payloadIndex.has_value()) << payloadIndex.error().message;
    ASSERT_TRUE(recipeIndex.has_value()) << recipeIndex.error().message;

    auto unitId = deriveSourceImportUnitId(SourceImporterKind::CatalogRecipe, "recipes/main.recipe");
    auto settingsHash = digestSourceImportSettings(std::span<const std::byte>{});
    ASSERT_TRUE(unitId.has_value()) << unitId.error().message;
    ASSERT_TRUE(settingsHash.has_value()) << settingsHash.error().message;
    candidate.units.push_back(SourceImportCapturedUnit{
        .unitId = *unitId,
        .importerKind = SourceImporterKind::CatalogRecipe,
        .importerVersion = 1U,
        .settingsHash = *settingsHash,
        .inputs =
            {
                SourceImportCapturedInput{.sourceIndex = *recipeIndex,
                                          .flags = AssetFormat::SourceImportInputFlags::Primary},
                SourceImportCapturedInput{.sourceIndex = *payloadIndex},
            },
        .outputs =
            {
                SourceImportCapturedOutput{.assetId = assetId(1U),
                                           .assetKind = AssetFormat::AssetKind::Texture2D},
            },
    });

    auto manifestHash = Core::digestContentHashV1(RecipeBytes);
    ASSERT_TRUE(manifestHash.has_value()) << manifestHash.error().message;
    auto bytes = writeSourceImportCandidateBytes(
        candidate,
        AssetFormat::SourceImportManifestRevision{.manifestDigest = *manifestHash, .manifestBytes = 64U});
    ASSERT_TRUE(bytes.has_value()) << bytes.error().message;
    auto view = AssetFormat::parseSourceImportMetadataView(*bytes);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    ASSERT_EQ(view->header().sourceCount, 2U);
    EXPECT_EQ(view->sourcePath(0U), std::optional<std::string_view>{"payload.bin"});
    EXPECT_EQ(view->sourcePath(1U), std::optional<std::string_view>{"recipes/main.recipe"});
    EXPECT_EQ(view->source(0U)->readExtent, AssetFormat::SourceImportReadExtent::Prefix);
    EXPECT_EQ(view->source(1U)->readExtent, AssetFormat::SourceImportReadExtent::WholeFile);
    ASSERT_EQ(view->header().unitCount, 1U);
    const auto primary = view->unitInputForUnit(0U, 1U);
    ASSERT_TRUE(primary.has_value());
    EXPECT_TRUE(AssetFormat::hasSourceImportInputFlag(primary->flags,
                                                      AssetFormat::SourceImportInputFlags::Primary));
}

TEST(SourceImportCaptureTests, DeduplicatesOnlyIdenticalSourceObservations)
{
    const auto root = std::filesystem::temp_directory_path() / "tina_source_import_dedup";
    const std::string rootUtf8 = toUtf8(root);
    const SourceImportCaptureConfig config{.sourceRootUtf8 = rootUtf8};
    constexpr std::array Bytes{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    constexpr std::array DifferentBytes{std::byte{'a'}, std::byte{'b'}, std::byte{'d'}};
    constexpr std::array ShortBytes{std::byte{'a'}, std::byte{'b'}};
    const auto sourcePath = toUtf8(root / "source.bin");

    SourceImportCandidate candidate{};
    const auto first = captureSourceImportBytes(candidate, config, sourcePath,
                                                AssetFormat::SourceImportReadExtent::WholeFile, Bytes);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    const auto duplicate = captureSourceImportBytes(candidate, config, sourcePath,
                                                    AssetFormat::SourceImportReadExtent::WholeFile, Bytes);
    ASSERT_TRUE(duplicate.has_value()) << duplicate.error().message;
    EXPECT_EQ(*duplicate, *first);
    EXPECT_EQ(candidate.sources.size(), 1U);

    const auto extentMismatch = captureSourceImportBytes(
        candidate, config, sourcePath, AssetFormat::SourceImportReadExtent::Prefix, Bytes);
    ASSERT_FALSE(extentMismatch.has_value());
    const auto hashMismatch = captureSourceImportBytes(
        candidate, config, sourcePath, AssetFormat::SourceImportReadExtent::WholeFile, DifferentBytes);
    ASSERT_FALSE(hashMismatch.has_value());
    const auto byteCountMismatch = captureSourceImportBytes(
        candidate, config, sourcePath, AssetFormat::SourceImportReadExtent::WholeFile, ShortBytes);
    ASSERT_FALSE(byteCountMismatch.has_value());
    EXPECT_EQ(candidate.sources.size(), 1U);
}

TEST(SourceImportCaptureTests, InvalidCandidateDoesNotReplaceExistingState)
{
    const auto root = std::filesystem::temp_directory_path() / "tina_source_import_commit";
    std::error_code errorCode;
    std::filesystem::remove_all(root, errorCode);
    const auto statePath = root / "import-state.tmeta";
    constexpr std::array Existing{std::byte{'o'}, std::byte{'l'}, std::byte{'d'}};
    ASSERT_TRUE(Core::writeFile(toUtf8(statePath), Existing).has_value());

    const auto status = commitSourceImportCandidate(toUtf8(statePath), SourceImportCandidate{}, {});
    ASSERT_FALSE(status.has_value());
    std::pmr::monotonic_buffer_resource memory;
    auto preserved = Core::readFile(toUtf8(statePath), Core::ReadFileConfig{.memoryResource = &memory});
    ASSERT_TRUE(preserved.has_value()) << preserved.error().message;
    EXPECT_TRUE(std::ranges::equal(*preserved, Existing));

    std::filesystem::remove_all(root, errorCode);
}

} // namespace
} // namespace Tina::Asset
