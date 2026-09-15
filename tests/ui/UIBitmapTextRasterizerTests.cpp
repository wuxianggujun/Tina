#include <tina/ui/text/UIBitmapTextRasterizer.hpp>
#include <tina/ui/text/UIGlyphAtlas.hpp>
#include <tina/ui/UIErrors.hpp>
#include "support/BitmapFontTestSupport.hpp"
#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>

namespace Tina::Tests {
namespace {
using namespace UI;
using namespace BitmapFontFixture;

TEST(UIBitmapTextRasterizerTests, ShapingIgnoresRasterBudgetsAndUsesUiLineBoxes)
{
    auto created = createBitmapTextRasterizer(atlas(), {.maxGlyphsPerRaster = 1, .coverageByteCapacity = 4, .glyphImageCapacity = 1});
    ASSERT_TRUE(created);
    auto& rasterizer = **created;
    const auto face = rasterizer.openFace({}).value();
    const UITextStyle style{.logicalSize = 8, .lineHeightScale = 2};
    auto shaped = rasterizer.shape(face, "AV\n中", style);
    ASSERT_TRUE(shaped);
    EXPECT_EQ(shaped->metrics.codepointCount, 4U);
    EXPECT_EQ(shaped->scalars.size(), 3U);
    EXPECT_EQ(shaped->metrics.lineCount, 2U);
    EXPECT_FLOAT_EQ(shaped->metrics.measuredSize.width, 9);
    EXPECT_FLOAT_EQ(shaped->metrics.measuredSize.height, 32);
    EXPECT_FLOAT_EQ(shaped->baselineFromLineTop, 11);
    EXPECT_EQ(shaped->scalars[2].clusterByteBegin, 3U);
    EXPECT_EQ(shaped->scalars[2].clusterByteEnd, 6U);
    EXPECT_EQ(shaped->scalars[2].line, 1U);
    EXPECT_FLOAT_EQ(shaped->scalars[1].visualStartX, 5);
    EXPECT_FLOAT_EQ(shaped->scalars[1].visualEndX, 9);
    EXPECT_TRUE(rasterizer.shape(face, std::string(5000, 'A'), style));
    auto rendered = rasterizer.raster(face, "A", style);
    ASSERT_FALSE(rendered);
    EXPECT_EQ(rendered.error().code, UIErrorCode::CapacityExceeded);
}

TEST(UIBitmapTextRasterizerTests, RasterCopiesOriginalImagesOnceAndDpiChangesOnlyLogicalGeometry)
{
    auto rasterizer = createBitmapTextRasterizer(atlas(), {.maxGlyphsPerRaster = 16, .coverageByteCapacity = 4096}).value();
    const auto face = rasterizer->openFace({}).value();
    const UITextStyle style{.logicalSize = 8, .lineHeightScale = 1.25F};
    auto batch = rasterizer->raster(face, "AAV", style, {1, 1});
    ASSERT_TRUE(batch);
    ASSERT_EQ(batch->glyphs.size(), 3U);
    EXPECT_EQ(batch->glyphs[0].coverageOffset, batch->glyphs[1].coverageOffset);
    EXPECT_EQ(batch->coverage.size(), 4U * 6U * 4U * 2U);
    EXPECT_EQ(batch->glyphs[0].imageKind, UIGlyphImageKind::BitmapCoverage);
    EXPECT_EQ(batch->glyphs[2].imageKind, UIGlyphImageKind::BitmapColor);
    EXPECT_EQ(batch->coverage[0], 128U); // source RGB was ignored for coverage
    const auto colorOffset = batch->glyphs[2].coverageOffset;
    EXPECT_EQ(batch->coverage[colorOffset], 40U);
    EXPECT_EQ(batch->coverage[colorOffset + 1], 80U);
    EXPECT_EQ(batch->coverage[colorOffset + 2], 120U);
    EXPECT_EQ(batch->coverage[colorOffset + 3], 128U);
    const auto firstGlyph = batch->glyphs[0];
    const auto originalMetrics = batch->metrics;
    const std::vector<Core::u8> originalPixels{batch->coverage.begin(), batch->coverage.end()};
    batch = rasterizer->raster(face, "AAV", style, {2, 3});
    ASSERT_TRUE(batch);
    EXPECT_EQ(batch->metrics, originalMetrics);
    EXPECT_EQ(batch->glyphs[0].rasterSize, firstGlyph.rasterSize);
    EXPECT_EQ(batch->glyphs[0].width, firstGlyph.width);
    EXPECT_EQ((std::vector<Core::u8>{batch->coverage.begin(), batch->coverage.end()}), originalPixels);
    auto enlargedStyle = style; enlargedStyle.logicalSize *= 2;
    batch = rasterizer->raster(face, "A", enlargedStyle);
    ASSERT_TRUE(batch);
    EXPECT_EQ(batch->glyphs[0].rasterSize, firstGlyph.rasterSize);
    EXPECT_FLOAT_EQ(batch->glyphs[0].logicalWidth, firstGlyph.logicalWidth * 2);
}

TEST(UIBitmapTextRasterizerTests, AtlasCachesBitmapKindsWithoutSizeOrDpiFragmentation)
{
    auto rasterizer = createBitmapTextRasterizer(atlas(), {.maxGlyphsPerRaster = 16, .coverageByteCapacity = 4096}).value();
    const auto face = rasterizer->openFace({}).value();
    auto batch = rasterizer->raster(face, "A", {.logicalSize = 8});
    ASSERT_TRUE(batch);
    const auto glyph = batch->glyphs.front();
    const UIGlyphKey key{glyph.face, glyph.glyphIndex, glyph.rasterSize, glyph.imageKind};
    auto atlasOwner = UIGlyphAtlas::Create({.width = 32, .height = 32, .maxGlyphs = 16}).value();
    const auto placement = atlasOwner->insert(key, glyph, batch->coverage);
    ASSERT_TRUE(placement);
    const auto revision = atlasOwner->pageRevision();
    batch = rasterizer->raster(face, "A", {.logicalSize = 24}, {3, 3});
    ASSERT_TRUE(batch);
    const auto scaled = batch->glyphs.front();
    const UIGlyphKey scaledKey{scaled.face, scaled.glyphIndex, scaled.rasterSize, scaled.imageKind};
    EXPECT_EQ(key, scaledKey);
    EXPECT_TRUE(atlasOwner->insert(scaledKey, scaled, batch->coverage));
    EXPECT_EQ(atlasOwner->pageRevision(), revision);
}

TEST(UIBitmapTextRasterizerTests, FaceGenerationsInputsAndSourceBudgetsFailClosed)
{
    EXPECT_FALSE(createBitmapTextRasterizer(nullptr));
    EXPECT_FALSE(createBitmapTextRasterizer(atlas(), {.maxFontBytes = 1}));
    auto rasterizer = createBitmapTextRasterizer(atlas(), {.maxTextBytes = 4}).value();
    EXPECT_FALSE(rasterizer->openFace({}, 1));
    const std::array bytes{std::byte{1}};
    EXPECT_FALSE(rasterizer->openFace(bytes));
    const auto first = rasterizer->openFace({}).value();
    EXPECT_FALSE(rasterizer->setFallbackChain(std::array{first}));
    EXPECT_FALSE(rasterizer->primeGlyphCache(first, {}));
    EXPECT_FALSE(rasterizer->shape(first, "AAAAA", {}));
    EXPECT_FALSE(rasterizer->shape(first, "A", {.direction = UITextDirection::RightToLeft}));
    EXPECT_FALSE(rasterizer->shape(first, std::string_view{"A\0", 2}, {}));
    EXPECT_FALSE(rasterizer->shape(first, "A", {.advanceScale = std::numeric_limits<float>::infinity()}));
    EXPECT_FALSE(rasterizer->raster(first, "A", {}, {0, 1}));
    ASSERT_TRUE(rasterizer->closeFace(first));
    const auto next = rasterizer->openFace({}).value();
    EXPECT_EQ(first.index, next.index);
    EXPECT_NE(first.generation, next.generation);
    EXPECT_FALSE(rasterizer->shape(first, "A", {}));
    EXPECT_TRUE(rasterizer->shape(next, "A", {}));
}

} // namespace
} // namespace Tina::Tests
