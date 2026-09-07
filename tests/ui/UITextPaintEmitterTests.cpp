#include <gtest/gtest.h>

#include "detail/UITextPaintEmitter.hpp"

#include <tina/ui/UIErrors.hpp>

#include <array>
#include <memory>
#include <memory_resource>
#include <utility>
#include <vector>

namespace Tina::Tests {
namespace {

[[nodiscard]] UI::UITextStyle testStyle() noexcept
{
    return UI::UITextStyle{
        .logicalSize = 10.0F,
        .advanceScale = 0.5F,
        .lineHeightScale = 1.5F,
    };
}

[[nodiscard]] UI::UIPremultipliedRgba8Color testColor() noexcept
{
    return UI::premultiply(UI::rgb(0xE6EDF3));
}

class BaselineRasterizer final : public UI::IUITextRasterizer {
  public:
    BaselineRasterizer() { m_coverage.fill(255); }
    [[nodiscard]] Core::Result<UI::UIFontFaceId> openFace(
        std::span<const std::byte> fontBytes, i32 faceIndex) override
    {
        if (!fontBytes.empty() || faceIndex != 0)
        {
            return Core::failure(UI::UIErrorCode::InvalidFont,
                                 "Baseline test rasterizer only exposes its built-in face");
        }
        return UI::UIFontFaceId{.index = 0, .generation = 1};
    }

    [[nodiscard]] Core::Status closeFace(UI::UIFontFaceId) noexcept override
    {
        return Core::success();
    }

    [[nodiscard]] Core::Result<UI::UITextMetrics> measure(
        UI::UIFontFaceId, std::string_view utf8, UI::UITextStyle style, UI::UITextRasterScale = {}) override
    {
        return UI::UITextMetrics{
            .measuredSize = {
                .width = utf8.empty() ? 0.0F : 5.0F,
                .height = utf8.empty() ? 0.0F : style.logicalSize * style.lineHeightScale,
            },
            .codepointCount = utf8.empty() ? 0U : 1U,
            .lineCount = utf8.empty() ? 0U : 1U,
        };
    }

    [[nodiscard]] Core::Result<UI::UITextRasterBatch> raster(
        UI::UIFontFaceId face, std::string_view utf8, UI::UITextStyle style, UI::UITextRasterScale = {}) override
    {
        auto metrics = measure(face, utf8, style);
        if (!metrics)
        {
            return Core::failure(metrics.error());
        }
        if (utf8 != "g")
        {
            return Core::failure(UI::UIErrorCode::InvalidText,
                                 "Baseline test rasterizer accepts only g");
        }
        return UI::UITextRasterBatch{
            .metrics = *metrics,
            .baselineFromLineTop = 8.0F,
            .glyphs = m_glyphs,
            .scalars = m_scalars,
            .coverage = m_coverage,
        };
    }

    Core::Status setFallbackChain(std::span<const UI::UIFontFaceId>) override { return Core::success(); }
    Core::Status primeGlyphCache(UI::UIFontFaceId, std::span<const std::byte>) override
    { return Core::failure(UI::UIErrorCode::InvalidFont, "Synthetic rasterizer does not accept cooked fonts"); }

    [[nodiscard]] UI::UITextRasterizerCapacity capacity() const noexcept override
    {
        return {.faceCapacity = 1, .maxGlyphsPerRaster = 1, .coverageByteCapacity = 160};
    }

  private:
    std::array<UI::UITextGlyphRaster, 1> m_glyphs{
        UI::UITextGlyphRaster{
            .face = {0, 1},
            .glyphIndex = static_cast<u32>('g'),
            .clusterByteEnd = 1,
            .advance = 5.0F,
            .bearingY = 7.0F,
            .width = 4,
            .height = 10,
            .coverageOffset = 0,
            .coveragePitch = 16,
            .logicalWidth = 4.0F, .logicalHeight = 10.0F, .rasterSize = {10, 10},
        },
    };
    std::array<UI::UITextScalarMetrics, 1> m_scalars{
        UI::UITextScalarMetrics{.advance = 5.0F, .visualEndX = 5.0F, .clusterByteEnd = 1, .hasVisualPosition = true}};
    std::array<u8, 160> m_coverage{};
};

class WideAdvanceRasterizer final : public UI::IUITextRasterizer {
  public:
    [[nodiscard]] Core::Result<UI::UIFontFaceId> openFace(
        std::span<const std::byte>, i32) override
    {
        return UI::UIFontFaceId{.index = 0, .generation = 1};
    }

    [[nodiscard]] Core::Status closeFace(UI::UIFontFaceId) noexcept override
    {
        return Core::success();
    }

    [[nodiscard]] Core::Result<UI::UITextMetrics> measure(
        UI::UIFontFaceId, std::string_view utf8, UI::UITextStyle style, UI::UITextRasterScale = {}) override
    {
        return UI::UITextMetrics{
            .measuredSize = {
                .width = static_cast<float>(utf8.size()) * 8.0F,
                .height = style.logicalSize * style.lineHeightScale,
            },
            .codepointCount = static_cast<u32>(utf8.size()),
            .lineCount = utf8.empty() ? 0U : 1U,
        };
    }

    [[nodiscard]] Core::Result<UI::UITextRasterBatch> raster(
        UI::UIFontFaceId face, std::string_view utf8, UI::UITextStyle style, UI::UITextRasterScale = {}) override
    {
        if (utf8 != "AB" && utf8 != "A" && utf8 != "B")
        {
            return Core::failure(UI::UIErrorCode::InvalidText,
                                 "Wide advance rasterizer accepts only AB");
        }
        auto metrics = measure(face, utf8, style);
        const usize begin = utf8 == "B" ? 1U : 0U;
        m_glyphs[begin].originX = 0.0F;
        if (utf8 == "AB") { m_glyphs[1].originX = 8.0F; }
        return UI::UITextRasterBatch{
            .metrics = *metrics,
            .baselineFromLineTop = 10.0F,
            .glyphs = std::span(m_glyphs).subspan(begin, utf8.size()),
            .scalars = std::span(m_scalars).first(utf8.size()),
            .coverage = m_coverage,
        };
    }

    Core::Status setFallbackChain(std::span<const UI::UIFontFaceId>) override { return Core::success(); }
    Core::Status primeGlyphCache(UI::UIFontFaceId, std::span<const std::byte>) override
    { return Core::failure(UI::UIErrorCode::InvalidFont, "Synthetic rasterizer does not accept cooked fonts"); }

    [[nodiscard]] UI::UITextRasterizerCapacity capacity() const noexcept override
    {
        return {.faceCapacity = 1, .maxGlyphsPerRaster = 2,
                .coverageByteCapacity = 8};
    }

  private:
    std::array<UI::UITextGlyphRaster, 2> m_glyphs{
        UI::UITextGlyphRaster{
            .face = {0, 1}, .glyphIndex = static_cast<u32>('A'), .clusterByteEnd = 1, .advance = 8.0F,
            .bearingY = 1.0F, .width = 1, .height = 1,
            .coverageOffset = 0, .coveragePitch = 4,
            .logicalWidth = 1.0F, .logicalHeight = 1.0F, .rasterSize = {10, 10},
        },
        UI::UITextGlyphRaster{
            .face = {0, 1}, .glyphIndex = static_cast<u32>('B'), .clusterByteBegin = 1, .clusterByteEnd = 2,
            .originX = 8.0F, .advance = 8.0F,
            .bearingY = 1.0F, .width = 1, .height = 1,
            .coverageOffset = 4, .coveragePitch = 4,
            .logicalWidth = 1.0F, .logicalHeight = 1.0F, .rasterSize = {10, 10},
        },
    };
    std::array<UI::UITextScalarMetrics, 2> m_scalars{
        UI::UITextScalarMetrics{.advance = 8.0F}, UI::UITextScalarMetrics{.advance = 8.0F}};
    std::array<u8, 8> m_coverage{255, 255, 255, 255, 255, 255, 255, 255};
};

TEST(UITextPaintEmitterTests, EmitsDeterministicFallbackAndRestoresBaseXAcrossChainedLines)
{
    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(2);
    u32 nextPaintOrdinal = 3;
    const UI::UICommittedLayoutEntry layoutEntry{
        .effectiveClip = {.x = 1.0F, .y = 2.0F, .width = 200.0F, .height = 100.0F},
    };
    UI::Detail::UITextPaintCursor cursor{
        .x = 10.0F,
        .y = 20.0F,
        .lineHeight = 15.0F,
        .baseX = 10.0F,
    };

    ASSERT_TRUE(UI::Detail::UITextPaintEmitter::append(output, layoutEntry, nextPaintOrdinal, "A", testStyle(), testColor(),
                                           cursor.x, cursor.y, {}, &cursor));
    ASSERT_TRUE(UI::Detail::UITextPaintEmitter::append(output, layoutEntry, nextPaintOrdinal, "\nB", testStyle(), testColor(),
                                           cursor.x, cursor.y, {}, &cursor));

    ASSERT_EQ(output.size(), 2U);
    EXPECT_EQ(output[0].kind, UI::UICommittedPaintKind::SolidQuad);
    EXPECT_FLOAT_EQ(output[0].worldRect.x, 10.0F);
    EXPECT_FLOAT_EQ(output[0].worldRect.y, 20.0F);
    EXPECT_FLOAT_EQ(output[0].worldRect.width, 5.0F);
    EXPECT_FLOAT_EQ(output[0].worldRect.height, 15.0F);
    EXPECT_EQ(output[0].paintOrdinal, 3U);
    EXPECT_EQ(output[0].effectiveClip, layoutEntry.effectiveClip);
    EXPECT_FLOAT_EQ(output[1].worldRect.x, 10.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.y, 35.0F);
    EXPECT_EQ(output[1].paintOrdinal, 4U);
    EXPECT_EQ(nextPaintOrdinal, 5U);
    EXPECT_FLOAT_EQ(cursor.x, 15.0F);
    EXPECT_FLOAT_EQ(cursor.y, 35.0F);
    EXPECT_FLOAT_EQ(cursor.baseX, 10.0F);
}

TEST(UITextPaintEmitterTests, EmitsAtlasGlyphsWhenRasterSourceIsAvailable)
{
    auto rasterizerResult = UI::createPlaceholderTextRasterizer();
    ASSERT_TRUE(rasterizerResult.has_value());
    std::unique_ptr<UI::IUITextRasterizer> rasterizer = std::move(*rasterizerResult);
    auto faceResult = rasterizer->openFace({});
    ASSERT_TRUE(faceResult.has_value());

    auto atlasResult = UI::UIGlyphAtlas::Create(UI::UIGlyphAtlasCapacity{
        .width = 64,
        .height = 64,
        .maxGlyphs = 4,
    });
    ASSERT_TRUE(atlasResult.has_value());
    std::unique_ptr<UI::UIGlyphAtlas> atlas = std::move(*atlasResult);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(2);
    u32 nextPaintOrdinal = 7;
    UI::Detail::UITextPaintCursor cursor{.x = 4.0F, .y = 6.0F, .baseX = 4.0F};
    ASSERT_TRUE(UI::Detail::UITextPaintEmitter::append(
        output, {}, nextPaintOrdinal, "AB", testStyle(), testColor(), cursor.x, cursor.y,
        UI::Detail::UITextPaintRasterSource{
            .rasterizer = rasterizer.get(),
            .face = *faceResult,
            .atlas = atlas.get(),
        },
        &cursor));

    ASSERT_EQ(output.size(), 2U);
    EXPECT_EQ(output[0].kind, UI::UICommittedPaintKind::Glyph);
    EXPECT_EQ(output[1].kind, UI::UICommittedPaintKind::Glyph);
    EXPECT_GT(output[0].atlasWidth, 0U);
    EXPECT_GT(output[0].atlasHeight, 0U);
    EXPECT_EQ(output[0].paintOrdinal, 7U);
    EXPECT_EQ(output[1].paintOrdinal, 8U);
    EXPECT_EQ(nextPaintOrdinal, 9U);
    EXPECT_GT(cursor.x, 4.0F);
}

TEST(UITextPaintEmitterTests, UsesRasterBaselineToKeepDescenderInsideLineBox)
{
    BaselineRasterizer rasterizer;
    auto face = rasterizer.openFace({}, 0);
    ASSERT_TRUE(face.has_value());
    auto atlasResult = UI::UIGlyphAtlas::Create(UI::UIGlyphAtlasCapacity{
        .width = 32,
        .height = 32,
        .maxGlyphs = 1,
    });
    ASSERT_TRUE(atlasResult.has_value());
    std::unique_ptr<UI::UIGlyphAtlas> atlas = std::move(*atlasResult);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(1);
    u32 nextPaintOrdinal = 0;
    const UI::UICommittedLayoutEntry layoutEntry{
        .effectiveClip = {.x = 0.0F, .y = 20.0F, .width = 100.0F, .height = 15.0F},
    };
    ASSERT_TRUE(UI::Detail::UITextPaintEmitter::append(
        output, layoutEntry, nextPaintOrdinal, "g", testStyle(), testColor(), 10.0F, 20.0F,
        UI::Detail::UITextPaintRasterSource{
            .rasterizer = &rasterizer,
            .face = *face,
            .atlas = atlas.get(),
        },
        nullptr));

    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0].kind, UI::UICommittedPaintKind::Glyph);
    EXPECT_FLOAT_EQ(output[0].worldRect.y, 21.0F);
    EXPECT_LE(output[0].worldRect.bottom(), layoutEntry.effectiveClip.bottom());
}

TEST(UITextPaintEmitterTests, AtlasExhaustionRollsBackPaintAndReportsErrorInsteadOfDrawingBoxes)
{
    auto rasterizerResult = UI::createPlaceholderTextRasterizer();
    ASSERT_TRUE(rasterizerResult.has_value());
    std::unique_ptr<UI::IUITextRasterizer> rasterizer = std::move(*rasterizerResult);
    auto faceResult = rasterizer->openFace({});
    ASSERT_TRUE(faceResult.has_value());

    auto atlasResult = UI::UIGlyphAtlas::Create(UI::UIGlyphAtlasCapacity{
        .width = 64,
        .height = 64,
        .maxGlyphs = 1,
    });
    ASSERT_TRUE(atlasResult.has_value());
    std::unique_ptr<UI::UIGlyphAtlas> atlas = std::move(*atlasResult);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(2);
    u32 nextPaintOrdinal = 11;
    UI::Detail::UITextPaintCursor cursor{.x = 2.0F, .y = 3.0F, .baseX = 2.0F};
    const auto status = UI::Detail::UITextPaintEmitter::append(
        output, {}, nextPaintOrdinal, "AB", testStyle(), testColor(), cursor.x, cursor.y,
        UI::Detail::UITextPaintRasterSource{
            .rasterizer = rasterizer.get(),
            .face = *faceResult,
            .atlas = atlas.get(),
        },
        &cursor);

    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(nextPaintOrdinal, 11U);
    EXPECT_EQ(atlas->statistics().glyphCount, 1U);
    EXPECT_FLOAT_EQ(cursor.x, 2.0F);
}

TEST(UITextPaintEmitterTests, WrappedAtlasExhaustionPreservesOutputAndCursor)
{
    WideAdvanceRasterizer rasterizer;
    auto face = rasterizer.openFace({}, 0);
    ASSERT_TRUE(face.has_value());
    auto atlasResult = UI::UIGlyphAtlas::Create(UI::UIGlyphAtlasCapacity{
        .width = 16,
        .height = 16,
        .maxGlyphs = 1,
    });
    ASSERT_TRUE(atlasResult.has_value());
    std::unique_ptr<UI::UIGlyphAtlas> atlas = std::move(*atlasResult);
    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(2);
    u32 nextPaintOrdinal = 4U;
    UI::Detail::UITextPaintCursor cursor{.x = 3.0F, .y = 7.0F, .baseX = 3.0F};

    const auto status = UI::Detail::UITextPaintEmitter::append(
        output, {}, nextPaintOrdinal, "AB", testStyle(), testColor(),
        cursor.x, cursor.y,
        {.rasterizer = &rasterizer, .face = *face, .atlas = atlas.get()},
        &cursor, 10.0F, UI::UITextWrapMode::Words);

    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_TRUE(output.empty());
    EXPECT_FLOAT_EQ(cursor.x, 3.0F);
    EXPECT_FLOAT_EQ(cursor.y, 7.0F);
    EXPECT_EQ(nextPaintOrdinal, 4U);
}

TEST(UITextPaintEmitterTests, LineClampCountsAndPaintsOnlyTheVisiblePrefixAndEllipsis)
{
    const UI::UITextStyle style = testStyle();
    const UI::Detail::UITextPaintRasterSource rasterSource{};
    EXPECT_EQ(UI::Detail::UITextPaintEmitter::countEntries(
                  "ABCD", style, rasterSource, 10.0F,
                  UI::UITextWrapMode::Words, {.maximumLines = 1}),
              2U);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    output.reserve(2);
    u32 nextPaintOrdinal = 3U;
    UI::Detail::UITextPaintCursor cursor{
        .x = 2.0F,
        .y = 4.0F,
        .baseX = 2.0F,
    };
    ASSERT_TRUE(UI::Detail::UITextPaintEmitter::append(
        output, {}, nextPaintOrdinal, "ABCD", style, testColor(),
        cursor.x, cursor.y, rasterSource, &cursor, 10.0F,
        UI::UITextWrapMode::Words, {.maximumLines = 1}));

    ASSERT_EQ(output.size(), 2U);
    EXPECT_FLOAT_EQ(output[0].worldRect.x, 2.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.x, 7.0F);
    EXPECT_FLOAT_EQ(output[0].worldRect.y, 4.0F);
    EXPECT_FLOAT_EQ(output[1].worldRect.y, 4.0F);
    EXPECT_EQ(output[0].paintOrdinal, 3U);
    EXPECT_EQ(output[1].paintOrdinal, 4U);
    EXPECT_FLOAT_EQ(cursor.x, 12.0F);
    EXPECT_FLOAT_EQ(cursor.y, 4.0F);
    EXPECT_EQ(nextPaintOrdinal, 5U);
}

TEST(UITextPaintEmitterTests, RasterizerWithoutAtlasReportsConfigurationError)
{
    WideAdvanceRasterizer rasterizer;
    auto face = rasterizer.openFace({}, 0);
    ASSERT_TRUE(face.has_value());
    const UI::Detail::UITextPaintRasterSource rasterSource{
        .rasterizer = &rasterizer,
        .face = *face,
        .atlas = nullptr,
    };
    const auto count = UI::Detail::UITextPaintEmitter::countEntries(
        "AB", testStyle(), rasterSource, 0.0F, UI::UITextWrapMode::NoWrap, {});
    ASSERT_TRUE(count);
    EXPECT_EQ(*count, 2U);

    std::pmr::vector<UI::UICommittedPaintEntry> output;
    u32 nextPaintOrdinal = 1U;
    UI::Detail::UITextPaintCursor cursor{};
    const auto status = UI::Detail::UITextPaintEmitter::append(
        output, {}, nextPaintOrdinal, "AB", testStyle(), testColor(),
        0.0F, 0.0F, rasterSource, &cursor);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidContextConfig);
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(nextPaintOrdinal, 1U);
}

} // namespace
} // namespace Tina::Tests
