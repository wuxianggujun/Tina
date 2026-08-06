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
        UI::UIFontFaceId, std::string_view utf8, UI::UITextStyle style) override
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
        UI::UIFontFaceId face, std::string_view utf8, UI::UITextStyle style) override
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
            .coverage = m_coverage,
        };
    }

    [[nodiscard]] UI::UITextRasterizerCapacity capacity() const noexcept override
    {
        return {.faceCapacity = 1, .maxGlyphsPerRaster = 1, .coverageByteCapacity = 40};
    }

  private:
    std::array<UI::UITextGlyphRaster, 1> m_glyphs{
        UI::UITextGlyphRaster{
            .codepoint = static_cast<u32>('g'),
            .advance = 5.0F,
            .bearingY = 7.0F,
            .width = 4,
            .height = 10,
            .coverageOffset = 0,
            .coveragePitch = 4,
        },
    };
    std::array<u8, 40> m_coverage{};
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

    UI::Detail::UITextPaintEmitter::append(output, layoutEntry, nextPaintOrdinal, "A", testStyle(), testColor(),
                                           cursor.x, cursor.y, {}, &cursor);
    UI::Detail::UITextPaintEmitter::append(output, layoutEntry, nextPaintOrdinal, "\nB", testStyle(), testColor(),
                                           cursor.x, cursor.y, {}, &cursor);

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
    UI::Detail::UITextPaintEmitter::append(
        output, {}, nextPaintOrdinal, "AB", testStyle(), testColor(), cursor.x, cursor.y,
        UI::Detail::UITextPaintRasterSource{
            .rasterizer = rasterizer.get(),
            .face = *faceResult,
            .atlas = atlas.get(),
        },
        &cursor);

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
    UI::Detail::UITextPaintEmitter::append(
        output, layoutEntry, nextPaintOrdinal, "g", testStyle(), testColor(), 10.0F, 20.0F,
        UI::Detail::UITextPaintRasterSource{
            .rasterizer = &rasterizer,
            .face = *face,
            .atlas = atlas.get(),
        },
        nullptr);

    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0].kind, UI::UICommittedPaintKind::Glyph);
    EXPECT_FLOAT_EQ(output[0].worldRect.y, 21.0F);
    EXPECT_LE(output[0].worldRect.bottom(), layoutEntry.effectiveClip.bottom());
}

TEST(UITextPaintEmitterTests, RollsBackPartialAtlasOutputAndOrdinalsBeforeFallback)
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
    UI::Detail::UITextPaintEmitter::append(
        output, {}, nextPaintOrdinal, "AB", testStyle(), testColor(), cursor.x, cursor.y,
        UI::Detail::UITextPaintRasterSource{
            .rasterizer = rasterizer.get(),
            .face = *faceResult,
            .atlas = atlas.get(),
        },
        &cursor);

    ASSERT_EQ(output.size(), 2U);
    EXPECT_EQ(output[0].kind, UI::UICommittedPaintKind::SolidQuad);
    EXPECT_EQ(output[1].kind, UI::UICommittedPaintKind::SolidQuad);
    EXPECT_EQ(output[0].paintOrdinal, 11U);
    EXPECT_EQ(output[1].paintOrdinal, 12U);
    EXPECT_EQ(nextPaintOrdinal, 13U);
    EXPECT_EQ(atlas->statistics().glyphCount, 1U);
    EXPECT_FLOAT_EQ(cursor.x, 12.0F);
}

} // namespace
} // namespace Tina::Tests
