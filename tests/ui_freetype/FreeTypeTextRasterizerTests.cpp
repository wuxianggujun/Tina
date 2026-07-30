#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] const char* resolveOptionalFontPath()
{
#if defined(TINA_UI_FREETYPE_TEST_FONT_PATH)
    return TINA_UI_FREETYPE_TEST_FONT_PATH;
#elif defined(TINA_UI_FONT_PATH)
    return TINA_UI_FONT_PATH;
#else
    if (const char* envPath = std::getenv("TINA_UI_FONT_PATH"); envPath != nullptr && envPath[0] != '\0')
    {
        return envPath;
    }
    return nullptr;
#endif
}

[[nodiscard]] std::vector<std::byte> loadFontBytes(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return {};
    }
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
    const char* fontPath = resolveOptionalFontPath();
    if (fontPath == nullptr)
    {
        GTEST_SKIP() << "No FreeType fixture font: set TINA_UI_FONT_PATH or CMake -DTINA_UI_FONT_PATH=";
    }
    const auto fontBytes = loadFontBytes(fontPath);
    if (fontBytes.empty())
    {
        GTEST_SKIP() << "Cannot read FreeType fixture font at " << fontPath;
    }

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

TEST(FreeTypeTextRasterizerTests, ContextSkipsZeroCoverageSpacePaintAndKeepsAtlasGlyphOrder)
{
    const char* fontPath = resolveOptionalFontPath();
    if (fontPath == nullptr) {
        GTEST_SKIP() << "No FreeType fixture font: set TINA_UI_FONT_PATH or CMake -DTINA_UI_FONT_PATH=";
    }
    const auto fontBytes = loadFontBytes(fontPath);
    if (fontBytes.empty()) {
        GTEST_SKIP() << "Cannot read FreeType fixture font at " << fontPath;
    }

    auto rasterizerResult = UI::createFreeTypeTextRasterizer(
        UI::UITextRasterizerCapacity{
            .faceCapacity = 1,
            .maxGlyphsPerRaster = 8,
            .coverageByteCapacity = 64U * 1024U,
        });
    ASSERT_TRUE(rasterizerResult.has_value())
        << (rasterizerResult ? "" : rasterizerResult.error().message);

    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    auto contextResult = UI::UIContext::Create(
        *windowResult,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 8,
        },
        std::move(*rasterizerResult));
    ASSERT_TRUE(contextResult.has_value())
        << (contextResult ? "" : contextResult.error().message);
    std::unique_ptr<UI::UIContext> context = std::move(*contextResult);
    ASSERT_TRUE(context->openTextFont(
        std::span<const std::byte>(fontBytes.data(), fontBytes.size())));

    auto rootResult = context->rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value())
        << (rootResult ? "" : rootResult.error().message);
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->treeUpdater(root);
    ASSERT_TRUE(updaterResult.has_value())
        << (updaterResult ? "" : updaterResult.error().message);
    UI::UITreeUpdater updater = std::move(*updaterResult);

    UI::UILayoutStyle rootStyle{};
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    ASSERT_TRUE(updater.setLayoutStyle(root.rootNodeId(), rootStyle));
    auto labelResult = updater.createElement(root.rootNodeId(), UI::makeLabelElement());
    ASSERT_TRUE(labelResult.has_value())
        << (labelResult ? "" : labelResult.error().message);
    const UI::UINodeId label = *labelResult;
    UI::UITextStyle textStyle{};
    textStyle.logicalSize = 24.0F;
    ASSERT_TRUE(updater.setTextStyle(label, textStyle));
    ASSERT_TRUE(updater.setText(label, "A A"));
    ASSERT_TRUE(context->commitLayout({.width = 200.0F, .height = 80.0F}));

    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 2U);
    for (const UI::UICommittedPaintEntry& entry : paint.entries()) {
        EXPECT_EQ(entry.node, label);
        EXPECT_TRUE(entry.isGlyph);
        EXPECT_GT(entry.atlasWidth, 0U);
        EXPECT_GT(entry.atlasHeight, 0U);
    }
    EXPECT_LT(paint.entries()[0].paintOrdinal, paint.entries()[1].paintOrdinal);
    EXPECT_LT(paint.entries()[0].worldRect.x, paint.entries()[1].worldRect.x);
    EXPECT_GT(
        paint.entries()[1].worldRect.x,
        paint.entries()[0].worldRect.x + paint.entries()[0].worldRect.width);

    // Repeated visible glyphs reuse one atlas placement; the zero-coverage
    // space consumes no slot and never becomes a SolidQuad paint entry.
    EXPECT_EQ(paint.entries()[0].atlasX, paint.entries()[1].atlasX);
    EXPECT_EQ(paint.entries()[0].atlasY, paint.entries()[1].atlasY);
    EXPECT_EQ(paint.entries()[0].atlasWidth, paint.entries()[1].atlasWidth);
    EXPECT_EQ(paint.entries()[0].atlasHeight, paint.entries()[1].atlasHeight);

    const std::span<const u8> atlas = context->glyphAtlasPixels();
    ASSERT_FALSE(atlas.empty());
    const u32 atlasWidth = context->glyphAtlasWidth();
    ASSERT_GT(atlasWidth, 0U);
    bool hasInk = false;
    for (u32 row = 0; row < paint.entries()[0].atlasHeight && !hasInk; ++row) {
        const usize rowOffset =
            static_cast<usize>(paint.entries()[0].atlasY + row) * atlasWidth
            + paint.entries()[0].atlasX;
        const auto rowPixels = atlas.subspan(rowOffset, paint.entries()[0].atlasWidth);
        hasInk = std::any_of(
            rowPixels.begin(), rowPixels.end(), [](u8 coverage) { return coverage != 0; });
    }
    EXPECT_TRUE(hasInk);
}

} // namespace Tina::Tests
