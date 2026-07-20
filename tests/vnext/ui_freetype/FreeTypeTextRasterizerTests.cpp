#include <gtest/gtest.h>

#include <tina/ui/UIErrors.hpp>
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>

#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace Tina::Tests {
namespace {

[[nodiscard]] std::vector<std::byte> loadFontBytes(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto size = static_cast<usize>(input.tellg());
    input.seekg(0, std::ios::beg);
    std::vector<std::byte> bytes(size);
    if (size > 0) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    }
    if (!input) {
        return {};
    }
    return bytes;
}

} // namespace

TEST(FreeTypeTextRasterizerTests, CreateRejectsEmptyFontBytesAndInvalidCapacity)
{
    auto rasterizerResult = UI::createFreeTypeTextRasterizer(
        UI::UITextRasterizerCapacity{.faceCapacity = 1});
    ASSERT_TRUE(rasterizerResult.has_value())
        << (rasterizerResult ? "" : rasterizerResult.error().message);
    std::unique_ptr<UI::IUITextRasterizer> rasterizer = std::move(*rasterizerResult);

    auto emptyFace = rasterizer->openFace({});
    ASSERT_FALSE(emptyFace.has_value());
    EXPECT_EQ(emptyFace.error().code, UI::UIErrorCode::InvalidFont);

    const std::byte junk[4]{
        std::byte{0},
        std::byte{1},
        std::byte{2},
        std::byte{3},
    };
    auto badFace = rasterizer->openFace(std::span<const std::byte>(junk, 4));
    ASSERT_FALSE(badFace.has_value());
    EXPECT_EQ(badFace.error().code, UI::UIErrorCode::InvalidFont);

    auto invalidCapacity = UI::createFreeTypeTextRasterizer(
        UI::UITextRasterizerCapacity{.faceCapacity = 0});
    ASSERT_FALSE(invalidCapacity.has_value());
    EXPECT_EQ(invalidCapacity.error().code, UI::UIErrorCode::InvalidContextConfig);
}

TEST(FreeTypeTextRasterizerTests, SourceHanSansFixtureMeasuresAndRastersChinese)
{
    const auto fontBytes = loadFontBytes(TINA_UI_FREETYPE_TEST_FONT_PATH);
    ASSERT_FALSE(fontBytes.empty())
        << "Missing test font at " << TINA_UI_FREETYPE_TEST_FONT_PATH;

    auto rasterizerResult = UI::createFreeTypeTextRasterizer(
        UI::UITextRasterizerCapacity{
            .faceCapacity = 1,
            .maxGlyphsPerRaster = 32,
            .coverageByteCapacity = 256U * 1024U,
        });
    ASSERT_TRUE(rasterizerResult.has_value())
        << (rasterizerResult ? "" : rasterizerResult.error().message);
    std::unique_ptr<UI::IUITextRasterizer> rasterizer = std::move(*rasterizerResult);

    auto faceResult = rasterizer->openFace(std::span<const std::byte>(fontBytes.data(), fontBytes.size()));
    ASSERT_TRUE(faceResult.has_value()) << (faceResult ? "" : faceResult.error().message);
    const UI::UIFontFaceId face = *faceResult;

    UI::UITextStyle style{};
    style.logicalSize = 24.0F;
    auto metrics = rasterizer->measure(face, "中文", style);
    ASSERT_TRUE(metrics.has_value()) << (metrics ? "" : metrics.error().message);
    EXPECT_EQ(metrics->codepointCount, 2U);
    EXPECT_EQ(metrics->lineCount, 1U);
    EXPECT_GT(metrics->measuredSize.width, 0.0F);
    EXPECT_GT(metrics->measuredSize.height, 0.0F);

    auto batch = rasterizer->raster(face, "中文", style);
    ASSERT_TRUE(batch.has_value()) << (batch ? "" : batch.error().message);
    ASSERT_EQ(batch->glyphs.size(), 2U);
    EXPECT_EQ(batch->glyphs[0].codepoint, 0x4E2DU);
    EXPECT_EQ(batch->glyphs[1].codepoint, 0x6587U);
    EXPECT_GT(batch->glyphs[0].advance, 0.0F);
    // Source Han should produce non-empty coverage for CJK at 24px.
    EXPECT_GT(batch->glyphs[0].width, 0U);
    EXPECT_GT(batch->glyphs[0].height, 0U);
    EXPECT_FALSE(batch->coverage.empty());
    // Top-left of a glyph box can be transparent; require some ink somewhere.
    bool hasInk = false;
    for (const u8 value : batch->coverage) {
        if (value != 0) {
            hasInk = true;
            break;
        }
    }
    EXPECT_TRUE(hasInk);

    ASSERT_TRUE(rasterizer->closeFace(face));
}

} // namespace Tina::Tests
