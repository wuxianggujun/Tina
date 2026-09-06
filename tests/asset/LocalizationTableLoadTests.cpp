#include <tina/asset/AssetErrors.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/asset/LocalizationTableLoad.hpp>
#include <tina/asset_format/AssetFormatErrors.hpp>
#include <tina/core/id/AssetId.hpp>
#include <tina/core/io/WriteFile.hpp>
#include <tina/localization/LocalizationErrors.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

namespace Tina::Asset {
namespace {

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

// Writes a strings file and returns the directory holding it. Caller removes the directory.
[[nodiscard]] std::filesystem::path writeStringsFile(std::string_view name, std::string_view contents)
{
    const auto root = std::filesystem::temp_directory_path() / "tina_localization_cook" / name;
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const auto file = root / "strings.loc";
    std::vector<std::byte> bytes(contents.size());
    for (std::size_t index = 0; index < contents.size(); ++index)
    {
        bytes[index] = static_cast<std::byte>(contents[index]);
    }
    const auto written = Core::writeFile(toUtf8(file), bytes);
    EXPECT_TRUE(written.has_value());
    return root;
}

[[nodiscard]] std::string recipeFor(std::string_view localeTag)
{
    return std::string("localization 0102030405060708090a0b0c0d0e0f10 ") + std::string(localeTag) +
           " strings.loc\n";
}

constexpr std::string_view SampleStrings =
    "# a comment line\n"
    "\n"
    "menu.play=Play\n"
    "menu.quit=Quit Game\n"
    "hud.score=Score: {0}\n";

TEST(LocalizationCookTests, CooksARecipeStringsFileIntoAParsablePayload)
{
    const auto root = writeStringsFile("basic", SampleStrings);
    auto request = parseCatalogCookRecipe(recipeFor("en-US"), toUtf8(root));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    ASSERT_EQ(request->assets.size(), 1U);
    EXPECT_EQ(request->assets[0].assetKind, AssetFormat::AssetKind::LocalizationTable);
    EXPECT_EQ(request->assets[0].assetTypeVersion, AssetFormat::LocalizationTableWire::SchemaVersion);

    const auto view = AssetFormat::parseLocalizationTablePayload(request->assets[0].payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->entryCount, 3U);
    EXPECT_EQ(view->localeTag, "en-US");
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("menu.play")), std::string_view("Play"));
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("menu.quit")), std::string_view("Quit Game"));
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("hud.score")), std::string_view("Score: {0}"));
    EXPECT_FALSE(view->find(AssetFormat::localizationKeyHash("menu.absent")).has_value());

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// Values with spaces are the reason the strings live in a file: the recipe tokenizer splits on
// whitespace, so an inline verb could not carry them at all.
TEST(LocalizationCookTests, PreservesSpacesAndTrailingWhitespaceInValues)
{
    const auto root = writeStringsFile("spaces",
                                       "label.prefix=Health: \n"
                                       "  indented.key = value with spaces \n");
    auto request = parseCatalogCookRecipe(recipeFor("en"), toUtf8(root));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    const auto view = AssetFormat::parseLocalizationTablePayload(request->assets[0].payload);
    ASSERT_TRUE(view.has_value());

    // The key side is trimmed, so indentation is allowed and the space before '=' is not part of
    // the key. The value side is NOT trimmed: a trailing space can be deliberate.
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("label.prefix")),
              std::string_view("Health: "));
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("indented.key")),
              std::string_view(" value with spaces "));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(LocalizationCookTests, SupportsEscapesAndValuesContainingEqualsSigns)
{
    const auto root = writeStringsFile("escapes",
                                       "multi.line=first\\nsecond\n"
                                       "tabbed=a\\tb\n"
                                       "path.windows=C:\\\\Games\\\\Tina\n"
                                       "literal.equals=a\\=b\n"
                                       "url=https://example.com/?a=1&b=2\n");
    auto request = parseCatalogCookRecipe(recipeFor("en"), toUtf8(root));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    const auto view = AssetFormat::parseLocalizationTablePayload(request->assets[0].payload);
    ASSERT_TRUE(view.has_value());

    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("multi.line")),
              std::string_view("first\nsecond"));
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("tabbed")), std::string_view("a\tb"));
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("path.windows")),
              std::string_view("C:\\Games\\Tina"));
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("literal.equals")), std::string_view("a=b"));
    // Only the FIRST unescaped '=' separates, so a value may contain '=' freely.
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("url")),
              std::string_view("https://example.com/?a=1&b=2"));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(LocalizationCookTests, AnEmptyValueIsLegalAndDistinctFromAnAbsentKey)
{
    const auto root = writeStringsFile("empty", "blanked.string=\nother=text\n");
    auto request = parseCatalogCookRecipe(recipeFor("en"), toUtf8(root));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    const auto view = AssetFormat::parseLocalizationTablePayload(request->assets[0].payload);
    ASSERT_TRUE(view.has_value());

    const auto blanked = view->find(AssetFormat::localizationKeyHash("blanked.string"));
    ASSERT_TRUE(blanked.has_value());
    EXPECT_TRUE(blanked->empty());
    EXPECT_FALSE(view->find(AssetFormat::localizationKeyHash("never.authored")).has_value());

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// The author writes in whatever order reads well; the cooker sorts by key hash because the wire
// requires it. Hash order has no relation to alphabetical order, so this cannot be the author's job.
TEST(LocalizationCookTests, SortsByKeyHashRegardlessOfAuthoredOrder)
{
    const auto root = writeStringsFile("order",
                                       "zebra=z\n"
                                       "alpha=a\n"
                                       "middle=m\n");
    auto request = parseCatalogCookRecipe(recipeFor("en"), toUtf8(root));
    ASSERT_TRUE(request.has_value()) << request.error().message;
    const auto view = AssetFormat::parseLocalizationTablePayload(request->assets[0].payload);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    ASSERT_EQ(view->entryCount, 3U);

    // The parser enforces strict ascending order, so a successful parse already proves the sort.
    // Asserting it directly documents what the payload holds.
    for (Core::u32 index = 1; index < view->entryCount; ++index)
    {
        const auto previous = view->entry(index - 1U);
        const auto current = view->entry(index);
        ASSERT_TRUE(previous.has_value() && current.has_value());
        EXPECT_LT(previous->keyHash, current->keyHash);
    }
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("alpha")), std::string_view("a"));
    EXPECT_EQ(view->find(AssetFormat::localizationKeyHash("zebra")), std::string_view("z"));

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(LocalizationCookTests, RejectsADuplicateKeyRatherThanShadowingOneValue)
{
    const auto root = writeStringsFile("duplicate", "menu.play=Play\nmenu.play=Start\n");
    const auto request = parseCatalogCookRecipe(recipeFor("en"), toUtf8(root));
    ASSERT_FALSE(request.has_value());
    EXPECT_EQ(request.error().code, AssetErrorCode::InvalidCatalogConfig);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(LocalizationCookTests, RejectsMalformedStringsLines)
{
    struct Case final {
        std::string_view name;
        std::string_view contents;
    };
    const Case cases[] = {
        {"no_separator", "menu.play\n"},
        {"empty_key", "=value\n"},
        {"dangling_escape", "menu.play=Play\\"},
        {"unknown_escape", "menu.play=Pl\\qay\n"},
    };
    for (const Case& testCase : cases)
    {
        const auto root = writeStringsFile(testCase.name, testCase.contents);
        const auto request = parseCatalogCookRecipe(recipeFor("en"), toUtf8(root));
        EXPECT_FALSE(request.has_value()) << testCase.name;
        if (!request.has_value())
        {
            EXPECT_EQ(request.error().code, AssetErrorCode::InvalidCatalogConfig) << testCase.name;
        }
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
}

TEST(LocalizationCookTests, RejectsAMalformedRecipeLineOrLocaleTag)
{
    const auto root = writeStringsFile("recipe_shape", SampleStrings);
    // Missing the file argument.
    EXPECT_FALSE(parseCatalogCookRecipe("localization 0102030405060708090a0b0c0d0e0f10 en-US\n",
                                        toUtf8(root)).has_value());
    // Not 32 hex digits.
    EXPECT_FALSE(parseCatalogCookRecipe("localization notanid en-US strings.loc\n",
                                        toUtf8(root)).has_value());
    // A locale tag the wire format refuses.
    const auto badTag = parseCatalogCookRecipe(recipeFor("this-tag-is-far-too-long-to-be-a-locale-tag"),
                                               toUtf8(root));
    EXPECT_FALSE(badTag.has_value());

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

// The whole point of the two halves: a payload the cooker produced must load into a runtime catalog
// and resolve the same strings by the same keys. A hash mismatch between the two sides would leave
// the table well formed and every lookup missing, which is why both use Core::stringKeyHash.
TEST(LocalizationTableLoadTests, CookedPayloadRoundTripsIntoAResolvableCatalog)
{
    const auto root = writeStringsFile("round_trip", SampleStrings);
    auto request = parseCatalogCookRecipe(recipeFor("zh-Hans-CN"), toUtf8(root));
    ASSERT_TRUE(request.has_value()) << request.error().message;

    std::pmr::unsynchronized_pool_resource memory;
    auto catalog = loadLocalizationCatalogFromPayload(request->assets[0].payload, {}, memory);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_EQ(catalog->locale().text(), "zh-Hans-CN");
    EXPECT_EQ(catalog->entryCount(), 3U);

    const auto playId = catalog->findTextId("menu.play");
    ASSERT_TRUE(playId.hasValue());
    const auto play = catalog->resolve(playId);
    ASSERT_TRUE(play.has_value());
    EXPECT_EQ(*play, "Play");
    EXPECT_EQ(catalog->resolveOr(catalog->findTextId("hud.score"), "?"), "Score: {0}");

    // An unknown key yields an invalid id, and resolving it says so rather than returning "".
    const auto unknown = catalog->findTextId("menu.never.authored");
    EXPECT_FALSE(unknown.hasValue());
    const auto unknownText = catalog->resolve(unknown);
    ASSERT_FALSE(unknownText.has_value());
    EXPECT_EQ(unknownText.error().code, Localization::LocalizationErrorCode::InvalidTextId);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(LocalizationTableLoadTests, DefaultConfigSizesCapacityToTheTableAndExplicitHeadroomIsKept)
{
    const auto root = writeStringsFile("capacity", SampleStrings);
    auto request = parseCatalogCookRecipe(recipeFor("en"), toUtf8(root));
    ASSERT_TRUE(request.has_value());

    std::pmr::unsynchronized_pool_resource memory;
    auto exact = loadLocalizationCatalogFromPayload(request->assets[0].payload, {}, memory);
    ASSERT_TRUE(exact.has_value()) << exact.error().message;
    EXPECT_EQ(exact->entryCapacity(), exact->entryCount());
    EXPECT_EQ(exact->textByteCapacity(), exact->textByteCount());

    auto roomy = loadLocalizationCatalogFromPayload(
        request->assets[0].payload, {.entryCapacity = 64, .textByteCapacity = 4096}, memory);
    ASSERT_TRUE(roomy.has_value()) << roomy.error().message;
    EXPECT_EQ(roomy->entryCapacity(), 64U);
    EXPECT_EQ(roomy->textByteCapacity(), 4096U);

    // An explicit capacity smaller than the table fails closed instead of being widened.
    const auto tooSmall = loadLocalizationCatalogFromPayload(
        request->assets[0].payload, {.entryCapacity = 1}, memory);
    ASSERT_FALSE(tooSmall.has_value());
    EXPECT_EQ(tooSmall.error().code, Localization::LocalizationErrorCode::CapacityExceeded);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(LocalizationTableLoadTests, IdsFromOneCatalogDoNotResolveAgainstAnother)
{
    const auto root = writeStringsFile("identity", SampleStrings);
    auto request = parseCatalogCookRecipe(recipeFor("en"), toUtf8(root));
    ASSERT_TRUE(request.has_value());

    std::pmr::unsynchronized_pool_resource memory;
    auto first = loadLocalizationCatalogFromPayload(request->assets[0].payload, {}, memory);
    auto second = loadLocalizationCatalogFromPayload(request->assets[0].payload, {}, memory);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first->catalogIdentity(), second->catalogIdentity());

    const auto id = first->findTextId("menu.play");
    ASSERT_TRUE(id.hasValue());
    EXPECT_TRUE(first->contains(id));
    EXPECT_FALSE(second->contains(id));
    const auto crossed = second->resolve(id);
    ASSERT_FALSE(crossed.has_value());
    EXPECT_EQ(crossed.error().code, Localization::LocalizationErrorCode::InvalidTextId);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST(LocalizationTableLoadTests, RejectsMalformedPayloadBytes)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::vector<std::byte> truncated(8, std::byte{0});
    const auto result = loadLocalizationCatalogFromPayload(truncated, {}, memory);
    ASSERT_FALSE(result.has_value());
    // Payload defects surface as AssetFormat errors, so a caller can tell a corrupt file from a
    // well-formed table the runtime refused.
    EXPECT_EQ(result.error().code.domain, Core::ErrorDomain::AssetFormat);
}

} // namespace
} // namespace Tina::Asset
