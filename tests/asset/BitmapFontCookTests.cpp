#include <tina/asset/AssetTypedViews.hpp>
#include <tina/asset/CatalogCook.hpp>
#include <tina/core/io/WriteFile.hpp>
#include "support/BitmapFontTestSupport.hpp"
#include "support/ImageFixtures.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory_resource>

namespace Tina::Tests {
namespace {
using namespace Asset;
using namespace AssetFormat;
using namespace BitmapFontFixture;

CookedAssetFile loadFile(std::span<const std::byte> bytes, std::pmr::memory_resource& memory)
{
    return makeCookedAssetFileFromBytes(std::pmr::vector<std::byte>{bytes.begin(), bytes.end(), &memory},
        {.memoryResource = &memory}).value();
}

TEST(BitmapFontAssetTests, TypedLoadOwnsMetricsAndPixelsAndResolvesPagesById)
{
    std::pmr::unsynchronized_pool_resource memory;
    const std::array ids{assetId(1), assetId(2)};
    auto fontFile = loadFile(writeCookedBitmapFontAsset(assetId(3), font(), ids).value(), memory);
    const auto source = pixels();
    std::array<CookedAssetFile, 2> pages;
    for (Core::usize index = 0; index < pages.size(); ++index) {
        const std::array levels{Texture2DLevelDesc{16, 16, std::as_bytes(std::span{source[index]})}};
        const auto bytes = writeCookedTexture2DAsset(ids[index], {
            .sampler = {.minFilter = Texture2DFilterMode::Point, .magFilter = Texture2DFilterMode::Point,
                        .mipFilter = Texture2DMipFilterMode::None}, .levels = levels});
        ASSERT_TRUE(bytes);
        pages[index] = loadFile(*bytes, memory);
    }
    auto atlas = loadBitmapFontAtlasFromCooked(fontFile, std::array{&pages[1], &pages[0]});
    ASSERT_TRUE(atlas) << atlas.error().message;
    EXPECT_EQ(atlas->pages(), source);
    EXPECT_FALSE(loadBitmapFontAtlasFromCooked(fontFile, std::array{&pages[0], &pages[0]}));
    auto metrics = parseBitmapFontFromCooked(fontFile);
    ASSERT_TRUE(metrics);
    fontFile = {}; pages[0] = {}; pages[1] = {};
    EXPECT_EQ(atlas->pages()[1][0], 80U);
    EXPECT_EQ(metrics->textureIds, (std::vector<Core::AssetId>{ids[0], ids[1]}));
    EXPECT_TRUE(metrics->font.contains('A'));
}

TEST(BitmapFontAssetTests, RejectsMissingRequiredDependenciesAndMismatchedOuterSchema)
{
    std::pmr::unsynchronized_pool_resource memory;
    const auto payload = writeBitmapFontPayloadBytes(font()).value();
    const std::array dependencies{CookedAssetWriteDependency{assetId(1), AssetKind::Texture2D, DependencyFlags::Required}};
    const auto bytes = writeCookedAssetBytes({.assetKind = AssetKind::Font, .assetId = assetId(3),
        .dependencies = dependencies, .payload = payload});
    ASSERT_TRUE(bytes);
    EXPECT_FALSE(parseBitmapFontFromCooked(loadFile(*bytes, memory)));
    const auto wrongSchema = writeCookedAssetBytes({.assetKind = AssetKind::Font, .assetTypeVersion = 2,
        .assetId = assetId(3), .payload = payload});
    ASSERT_TRUE(wrongSchema);
    EXPECT_FALSE(parseBitmapFontFromCooked(loadFile(*wrongSchema, memory)));
}

std::string pathUtf8(const std::filesystem::path& path)
{
    const auto utf8 = path.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

constexpr std::string_view FontSource = R"({"schema":1,"nominalSize":2,"lineHeight":3,"baseline":2,"fallback":63,
    "pages":[{"textureId":"02000000000000000000000000000000","image":"font.png","kind":"color"},
             {"textureId":"01000000000000000000000000000000","image":"font.png","kind":"coverage"}],
    "glyphs":[{"codepoint":63,"page":0,"rect":[0,0,1,2],"advance":2,"bearing":[0,2]},
              {"codepoint":65,"page":1,"rect":[1,0,1,2],"advance":2,"bearing":[0,2]}],"kerning":[]})";
constexpr std::string_view Recipe = "platform WindowsX64\nbitmapfont 03000000000000000000000000000000 font.json\n";

class BitmapFontCookTests : public testing::Test {
  protected:
    void SetUp() override {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("tina_bitmap_font_" + std::to_string(unique));
        std::filesystem::create_directories(root_);
        rootUtf8_ = pathUtf8(root_);
        ASSERT_TRUE(Core::writeFile(pathUtf8(root_ / "font.png"), ImageFixtures::tinyPngBytes()));
        writeSource(FontSource);
        ASSERT_TRUE(Core::writeFile(pathUtf8(root_ / "font.recipe"), std::as_bytes(std::span{Recipe.data(), Recipe.size()})));
    }
    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        EXPECT_FALSE(error) << error.message();
    }
    void writeSource(std::string_view text) {
        ASSERT_TRUE(Core::writeFile(pathUtf8(root_ / "font.json"), std::as_bytes(std::span{text.data(), text.size()})));
    }
    std::filesystem::path root_;
    std::string rootUtf8_;
};

TEST_F(BitmapFontCookTests, SourceCookCapturesJsonPngAndCanonicalizesPageIndices)
{
    const auto imported = loadCatalogCookRecipeSourceFile(pathUtf8(root_ / "font.recipe"), {.sourceRootUtf8 = rootUtf8_});
    ASSERT_TRUE(imported) << imported.error().message;
    ASSERT_EQ(imported->request.assets.size(), 3U);
    EXPECT_EQ(imported->sourceImports.sources.size(), 3U); // recipe, JSON, one shared PNG
    ASSERT_EQ(imported->sourceImports.units.size(), 1U);
    EXPECT_EQ(imported->sourceImports.units.front().importerVersion, 4U);
    const auto& spec = imported->request.assets.back();
    ASSERT_EQ(spec.assetKind, AssetKind::Font);
    ASSERT_EQ(spec.dependencies.size(), 2U);
    EXPECT_EQ(spec.dependencies[0].assetId, assetId(1));
    auto metrics = parseBitmapFontPayload(spec.payload);
    ASSERT_TRUE(metrics);
    EXPECT_EQ(metrics->descriptor().glyphs[metrics->glyphIndex('?')].page, 1U);
    EXPECT_EQ(metrics->descriptor().glyphs[metrics->glyphIndex('A')].page, 0U);
    auto cooked = cookCatalogPackage(imported->request);
    ASSERT_TRUE(cooked) << cooked.error().message;
    EXPECT_EQ(cooked->entryCount, 3U);
    auto invalid = imported->request;
    invalid.assets.front().payload[13] = static_cast<std::byte>(Texture2DFilterMode::Linear);
    EXPECT_FALSE(cookCatalogPackage(invalid));
    EXPECT_FALSE(loadCatalogCookRecipeSourceFile(pathUtf8(root_ / "font.recipe"), {.sourceRootUtf8 = rootUtf8_, .maxSources = 2}));
}

TEST_F(BitmapFontCookTests, RejectsMalformedSourceTraversalAndNonPngWithoutFallback)
{
    std::string source{FontSource};
    source.replace(source.find("font.png"), 8, "../outside.png");
    writeSource(source);
    EXPECT_FALSE(parseCatalogCookRecipe(Recipe, rootUtf8_));
    source = FontSource;
    source.replace(source.find("\"schema\":1"), 10, "\"schema\":2");
    writeSource(source);
    EXPECT_FALSE(parseCatalogCookRecipe(Recipe, rootUtf8_));
    writeSource(FontSource);
    const std::array invalidImage{std::byte{1}, std::byte{2}, std::byte{3}};
    ASSERT_TRUE(Core::writeFile(pathUtf8(root_ / "font.png"), invalidImage));
    EXPECT_FALSE(parseCatalogCookRecipe(Recipe, rootUtf8_));
}

} // namespace
} // namespace Tina::Tests
