#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>
#include <tina/ui/text/UIGlyphAtlas.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <vector>

namespace Tina::Tests {
namespace {

[[nodiscard]] UI::UIFontFaceId makeFace(u32 index, u32 generation)
{
    return UI::UIFontFaceId{.index = index, .generation = generation};
}

[[nodiscard]] UI::UITextGlyphRaster makeGlyph(
    u32 codepoint,
    u32 width,
    u32 height,
    float advance)
{
    return UI::UITextGlyphRaster{
        .codepoint = codepoint,
        .advance = advance,
        .bearingX = 0.0F,
        .bearingY = static_cast<float>(height),
        .width = width,
        .height = height,
        .coverageOffset = 0,
        .coveragePitch = width,
    };
}

} // namespace

TEST(UIGlyphAtlasTests, InsertFindAndReuseSameKey)
{
    auto atlasResult = UI::UIGlyphAtlas::Create(UI::UIGlyphAtlasCapacity{
        .width = 32,
        .height = 32,
        .maxGlyphs = 8,
    });
    ASSERT_TRUE(atlasResult.has_value()) << (atlasResult ? "" : atlasResult.error().message);
    auto& atlas = **atlasResult;

    const std::vector<u8> coverage(4 * 4, 200);
    const UI::UIGlyphKey key{
        .face = makeFace(0, 1),
        .codepoint = 'A',
        .pixelSize = 16,
    };
    auto first = atlas.insert(key, makeGlyph('A', 4, 4, 5.0F), coverage);
    ASSERT_TRUE(first.has_value()) << (first ? "" : first.error().message);
    EXPECT_TRUE(first->id.hasValue());
    EXPECT_EQ(first->width, 4U);
    EXPECT_EQ(first->height, 4U);
    EXPECT_EQ(first->atlasX, 0U);
    EXPECT_EQ(first->atlasY, 0U);

    auto found = atlas.find(key);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, first->id);

    auto second = atlas.insert(key, makeGlyph('A', 4, 4, 5.0F), coverage);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->id, first->id);
    EXPECT_EQ(atlas.statistics().glyphCount, 1U);

    const auto page = atlas.pagePixels();
    EXPECT_EQ(page.size(), 32U * 32U);
    EXPECT_EQ(page[0], 200);
}

TEST(UIGlyphAtlasTests, PacksOnShelvesAndRejectsCapacity)
{
    auto atlasResult = UI::UIGlyphAtlas::Create(UI::UIGlyphAtlasCapacity{
        .width = 8,
        .height = 8,
        .maxGlyphs = 2,
    });
    ASSERT_TRUE(atlasResult.has_value());
    auto& atlas = **atlasResult;

    const std::vector<u8> a(4 * 4, 1);
    const std::vector<u8> b(4 * 4, 2);
    const std::vector<u8> c(4 * 4, 3);

    ASSERT_TRUE(atlas.insert(
        UI::UIGlyphKey{.face = makeFace(0, 1), .codepoint = 'A', .pixelSize = 8},
        makeGlyph('A', 4, 4, 4.0F),
        a));
    auto second = atlas.insert(
        UI::UIGlyphKey{.face = makeFace(0, 1), .codepoint = 'B', .pixelSize = 8},
        makeGlyph('B', 4, 4, 4.0F),
        b);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->atlasX, 4U);
    EXPECT_EQ(second->atlasY, 0U);

    auto third = atlas.insert(
        UI::UIGlyphKey{.face = makeFace(0, 1), .codepoint = 'C', .pixelSize = 8},
        makeGlyph('C', 4, 4, 4.0F),
        c);
    ASSERT_FALSE(third.has_value());
    EXPECT_EQ(third.error().code, UI::UIErrorCode::CapacityExceeded);
}

TEST(UIGlyphAtlasTests, ClearInvalidatesIdsAndAllowsRepack)
{
    auto atlasResult = UI::UIGlyphAtlas::Create(UI::UIGlyphAtlasCapacity{
        .width = 16,
        .height = 16,
        .maxGlyphs = 4,
    });
    ASSERT_TRUE(atlasResult.has_value());
    auto& atlas = **atlasResult;

    const std::vector<u8> coverage(2 * 2, 255);
    const UI::UIGlyphKey key{
        .face = makeFace(1, 2),
        .codepoint = 'Z',
        .pixelSize = 12,
    };
    auto placed = atlas.insert(key, makeGlyph('Z', 2, 2, 3.0F), coverage);
    ASSERT_TRUE(placed.has_value());
    const UI::UIGlyphId oldId = placed->id;
    EXPECT_TRUE(atlas.contains(oldId));

    atlas.clear();
    EXPECT_FALSE(atlas.contains(oldId));
    EXPECT_FALSE(atlas.find(key).has_value());
    EXPECT_EQ(atlas.statistics().glyphCount, 0U);
    EXPECT_EQ(atlas.pagePixels()[0], 0);

    auto again = atlas.insert(key, makeGlyph('Z', 2, 2, 3.0F), coverage);
    ASSERT_TRUE(again.has_value());
    EXPECT_NE(again->id.generation, oldId.generation);
    EXPECT_TRUE(atlas.contains(again->id));
}

TEST(UIGlyphAtlasTests, ZeroSizedGlyphStoresAdvanceWithoutPixels)
{
    auto atlasResult = UI::UIGlyphAtlas::Create(UI::UIGlyphAtlasCapacity{
        .width = 8,
        .height = 8,
        .maxGlyphs = 2,
    });
    ASSERT_TRUE(atlasResult.has_value());
    auto& atlas = **atlasResult;
    auto placed = atlas.insert(
        UI::UIGlyphKey{.face = makeFace(0, 1), .codepoint = ' ', .pixelSize = 16},
        makeGlyph(' ', 0, 0, 4.0F),
        {});
    ASSERT_TRUE(placed.has_value());
    EXPECT_EQ(placed->width, 0U);
    EXPECT_EQ(placed->height, 0U);
    EXPECT_FLOAT_EQ(placed->advance, 4.0F);
    EXPECT_EQ(atlas.statistics().usedPixels, 0U);
}

TEST(UIGlyphAtlasTests, IntegratesWithPlaceholderRasterizerCoverage)
{
    auto rasterizerResult = UI::createPlaceholderTextRasterizer();
    ASSERT_TRUE(rasterizerResult.has_value());
    auto& rasterizer = *rasterizerResult;
    auto faceResult = rasterizer->openFace({});
    ASSERT_TRUE(faceResult.has_value());
    const UI::UIFontFaceId face = *faceResult;
    auto batch = rasterizer->raster(face, "Hi", {});
    ASSERT_TRUE(batch.has_value());
    ASSERT_EQ(batch->glyphs.size(), 2U);

    auto atlasResult = UI::UIGlyphAtlas::Create(UI::UIGlyphAtlasCapacity{
        .width = 128,
        .height = 64,
        .maxGlyphs = 16,
    });
    ASSERT_TRUE(atlasResult.has_value());
    auto& atlas = **atlasResult;

    for (const UI::UITextGlyphRaster& glyph : batch->glyphs) {
        const std::span<const u8> coverage(
            batch->coverage.data() + glyph.coverageOffset,
            static_cast<usize>(glyph.width) * glyph.height);
        auto placed = atlas.insert(
            UI::UIGlyphKey{
                .face = face,
                .codepoint = glyph.codepoint,
                .pixelSize = 16,
            },
            glyph,
            coverage);
        ASSERT_TRUE(placed.has_value()) << (placed ? "" : placed.error().message);
        EXPECT_GT(placed->width, 0U);
        EXPECT_GT(placed->height, 0U);
    }
    EXPECT_EQ(atlas.statistics().glyphCount, 2U);
}

} // namespace Tina::Tests
