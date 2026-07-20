#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <memory_resource>
#include <span>
#include <string_view>

namespace Tina::Tests {
namespace {

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

} // namespace

TEST(UITextRasterizerTests, PlaceholderOpenMeasureRasterAndClose)
{
    auto rasterizerResult = UI::createPlaceholderTextRasterizer(
        UI::UITextRasterizerCapacity{
            .faceCapacity = 2,
            .maxGlyphsPerRaster = 32,
            .coverageByteCapacity = 64U * 1024U,
        });
    ASSERT_TRUE(rasterizerResult.has_value())
        << (rasterizerResult ? "" : rasterizerResult.error().message);
    std::unique_ptr<UI::IUITextRasterizer> rasterizer = std::move(*rasterizerResult);

    auto faceResult = rasterizer->openFace({});
    ASSERT_TRUE(faceResult.has_value()) << (faceResult ? "" : faceResult.error().message);
    const UI::UIFontFaceId face = *faceResult;
    EXPECT_TRUE(face.hasValue());

    auto metrics = rasterizer->measure(face, "AB", {});
    ASSERT_TRUE(metrics.has_value()) << (metrics ? "" : metrics.error().message);
    EXPECT_EQ(metrics->codepointCount, 2U);
    EXPECT_EQ(metrics->lineCount, 1U);
    EXPECT_FLOAT_EQ(metrics->measuredSize.width, 16.0F * 0.6F * 2.0F);

    auto batch = rasterizer->raster(face, "AB", {});
    ASSERT_TRUE(batch.has_value()) << (batch ? "" : batch.error().message);
    ASSERT_EQ(batch->glyphs.size(), 2U);
    EXPECT_EQ(batch->glyphs[0].codepoint, static_cast<u32>('A'));
    EXPECT_EQ(batch->glyphs[1].codepoint, static_cast<u32>('B'));
    EXPECT_GT(batch->glyphs[0].width, 0U);
    EXPECT_GT(batch->glyphs[0].height, 0U);
    EXPECT_EQ(
        batch->coverage.size(),
        static_cast<usize>(batch->glyphs[0].width) * batch->glyphs[0].height
            + static_cast<usize>(batch->glyphs[1].width) * batch->glyphs[1].height);
    EXPECT_EQ(batch->coverage.front(), 255);

    auto cjk = rasterizer->raster(face, "中", {});
    ASSERT_TRUE(cjk.has_value()) << (cjk ? "" : cjk.error().message);
    ASSERT_EQ(cjk->glyphs.size(), 1U);
    EXPECT_EQ(cjk->glyphs[0].codepoint, 0x4E2DU);

    assertOk(rasterizer->closeFace(face));
    auto closed = rasterizer->measure(face, "A", {});
    ASSERT_FALSE(closed.has_value());
    EXPECT_EQ(closed.error().code, UI::UIErrorCode::InvalidFont);
}

TEST(UITextRasterizerTests, PlaceholderRejectsNonEmptyFontBytesAndCapacity)
{
    auto rasterizerResult = UI::createPlaceholderTextRasterizer(
        UI::UITextRasterizerCapacity{.faceCapacity = 1});
    ASSERT_TRUE(rasterizerResult.has_value());
    std::unique_ptr<UI::IUITextRasterizer> rasterizer = std::move(*rasterizerResult);

    const std::byte blob[1]{std::byte{0}};
    auto nonEmpty = rasterizer->openFace(std::span<const std::byte>(blob, 1));
    ASSERT_FALSE(nonEmpty.has_value());
    EXPECT_EQ(nonEmpty.error().code, UI::UIErrorCode::InvalidFont);

    auto first = rasterizer->openFace({});
    ASSERT_TRUE(first.has_value());
    auto second = rasterizer->openFace({});
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, UI::UIErrorCode::CapacityExceeded);

    auto invalidCapacity = UI::createPlaceholderTextRasterizer(
        UI::UITextRasterizerCapacity{.faceCapacity = 0});
    ASSERT_FALSE(invalidCapacity.has_value());
    EXPECT_EQ(invalidCapacity.error().code, UI::UIErrorCode::InvalidContextConfig);
}

TEST(UITextRasterizerTests, PlaceholderNewlinesDoNotEmitGlyphs)
{
    auto rasterizer = *UI::createPlaceholderTextRasterizer();
    auto face = *rasterizer->openFace({});
    auto batch = rasterizer->raster(face, "A\nB", {});
    ASSERT_TRUE(batch.has_value());
    ASSERT_EQ(batch->glyphs.size(), 2U);
    EXPECT_EQ(batch->metrics.lineCount, 2U);
}

} // namespace Tina::Tests
