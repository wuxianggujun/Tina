#include <tina/text/BitmapFont.hpp>
#include "support/BitmapFontTestSupport.hpp"
#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>

namespace Tina::Tests {
namespace {
using namespace BitmapFontFixture;

TEST(BitmapFontTests, NormalizesGlyphOrderAndKeepsAnExplicitFallback)
{
    const auto metrics = font();
    EXPECT_EQ(metrics.descriptor().glyphs.front().codepoint, ' ');
    EXPECT_TRUE(metrics.contains('A'));
    EXPECT_TRUE(metrics.contains(0x4E2D));
    EXPECT_FALSE(metrics.contains('Z'));
    EXPECT_EQ(metrics.glyphIndex('Z'), metrics.glyphIndex('?'));
    EXPECT_FLOAT_EQ(metrics.kerning('A', 'V'), -1);
    EXPECT_FLOAT_EQ(metrics.kerning('V', 'A'), 0);
}

TEST(BitmapFontTests, Utf8KerningExplicitLinesAndFallbackKeepScalarByteMapping)
{
    auto result = Text::layoutBitmapText(font(), "AV中?\nZ ");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->scalarCount, 7U);
    EXPECT_EQ(result->lineCount, 2U);
    EXPECT_EQ(result->missingGlyphCount, 1U);
    EXPECT_FLOAT_EQ(result->width, 21);
    EXPECT_FLOAT_EQ(result->height, 20);
    EXPECT_FLOAT_EQ(result->baseline, 8);
    ASSERT_EQ(result->glyphs.size(), 6U);
    EXPECT_FLOAT_EQ(result->glyphs[1].originX, 4);
    EXPECT_FLOAT_EQ(result->glyphs[1].advance, 4);
    EXPECT_EQ(result->glyphs[2].byteBegin, 2U);
    EXPECT_EQ(result->glyphs[2].byteEnd, 5U);
    EXPECT_EQ(result->glyphs[4].scalarIndex, 5U);
    EXPECT_EQ(result->glyphs[4].line, 1U);
    EXPECT_FLOAT_EQ(result->glyphs[4].baselineY, 18);
    EXPECT_TRUE(result->glyphs[4].missing);
}

TEST(BitmapFontTests, ScratchLayoutMatchesOwningLayoutAndWrapDropsCrossLineKerning)
{
    const auto metrics = font();
    std::array<Text::BitmapTextGlyph, 3> storage{};
    auto view = Text::layoutBitmapTextInto(metrics, "AVA", {.wrapWidth = 9}, storage);
    ASSERT_TRUE(view);
    EXPECT_EQ(view->glyphs.data(), storage.data());
    EXPECT_EQ(view->lineCount, 2U);
    EXPECT_FLOAT_EQ(view->width, 9);
    EXPECT_FLOAT_EQ(view->glyphs[2].originX, 0);
    EXPECT_EQ(view->glyphs[2].line, 1U);
    const auto scaled = Text::layoutBitmapText(metrics, "AV\nA", {.scale = 2, .extraLineSpacing = 3});
    ASSERT_TRUE(scaled);
    EXPECT_FLOAT_EQ(scaled->width, 18);
    EXPECT_FLOAT_EQ(scaled->height, 43);
    EXPECT_FLOAT_EQ(scaled->glyphs[2].baselineY, 39);
    EXPECT_FALSE(Text::layoutBitmapTextInto(metrics, "AAAA", {}, storage));
}

TEST(BitmapFontTests, RejectsInvalidUtf8NulLimitsAndOverflow)
{
    const auto metrics = font();
    for (const std::string text : {std::string{"A\0B", 3}, std::string{"\xC0\xAF"}, std::string{"\xED\xA0\x80"}}) {
        auto invalid = Text::layoutBitmapText(metrics, text);
        ASSERT_FALSE(invalid);
        EXPECT_EQ(invalid.error().code, Text::TextErrorCode::InvalidText);
    }
    EXPECT_FALSE(Text::layoutBitmapText(metrics, "AV", {.maxScalars = 1}));
    EXPECT_FALSE(Text::layoutBitmapText(metrics, "中", {.maxTextBytes = 2}));
    EXPECT_FALSE(Text::layoutBitmapText(metrics, "A", {.scale = 0}));
    EXPECT_FALSE(Text::layoutBitmapText(metrics, "A", {.extraLineSpacing = -10}));
    EXPECT_FALSE(Text::layoutBitmapText(metrics, "", {.scale = (std::numeric_limits<float>::max)()}));
    const auto empty = Text::layoutBitmapText(metrics, "");
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->lineCount, 0U);
    EXPECT_FLOAT_EQ(empty->height, 0);
    EXPECT_TRUE(empty->glyphs.empty());
}

TEST(BitmapFontTests, RejectsDuplicateScalarsBoundsMissingFallbackAndBackwardKerning)
{
    auto source = descriptor();
    source.glyphs.push_back(source.glyphs.front());
    EXPECT_FALSE(Text::BitmapFont::Create(source));
    source = descriptor(); source.glyphs.front().codepoint = 0xD800;
    EXPECT_FALSE(Text::BitmapFont::Create(source));
    source = descriptor(); source.glyphs.front().x = 15;
    EXPECT_FALSE(Text::BitmapFont::Create(source));
    source = descriptor(); source.fallbackCodepoint = 0xFFFD;
    EXPECT_FALSE(Text::BitmapFont::Create(source));
    source = descriptor(); source.kerning.front().adjustment = -6;
    EXPECT_FALSE(Text::BitmapFont::Create(source));
    source = descriptor(); source.pages.front().width = Text::BitmapFont::MaxPageDimension + 1;
    EXPECT_FALSE(Text::BitmapFont::Create(source));
    source = descriptor(); source.baseline = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Text::BitmapFont::Create(source));
}

TEST(BitmapFontTests, CpuAtlasOwnsExactRgbaPageExtents)
{
    auto source = pixels();
    auto result = Text::BitmapFontAtlas::Create(font(), source);
    ASSERT_TRUE(result);
    source[0][0] = 0;
    EXPECT_EQ(result->pages()[0][0], 200U);
    source.pop_back();
    EXPECT_FALSE(Text::BitmapFontAtlas::Create(font(), source));
    source = pixels(); source[1].pop_back();
    EXPECT_FALSE(Text::BitmapFontAtlas::Create(font(), source));
}

} // namespace
} // namespace Tina::Tests
