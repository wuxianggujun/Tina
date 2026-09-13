#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>
#include <tina/ui/UIErrors.hpp>
#include <tina/ui/text/FreeTypeTextRasterizerFactory.hpp>
#include <tina/ui/text/TextShaper.h>
#include <tina/ui/text/UIBakedFont.hpp>
#include "detail/UITextWrapping.hpp"
#include "detail/UITextTruncation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
    const auto utf8 = std::u8string_view(reinterpret_cast<const char8_t*>(path), std::strlen(path));
    std::ifstream input(std::filesystem::path{utf8}, std::ios::binary);
    if (!input) {
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end <= 0 || end > 64 * 1024 * 1024) { return {}; }
    const auto size = static_cast<usize>(end);
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

std::vector<std::byte> systemTestFont(const char* name)
{
#if defined(_WIN32)
    const wchar_t* directory = _wgetenv(L"WINDIR");
    if (directory == nullptr) { return {}; }
    const auto path = (std::filesystem::path{directory} / "Fonts" / name).u8string();
    return loadFontBytes(reinterpret_cast<const char*>(path.c_str()));
#else
    (void)name;
    return {};
#endif
}

void writeWord(std::vector<std::byte>& bytes, usize offset, u32 value)
{
    for (u32 index = 0; index < 4; ++index) { bytes[offset + index] = static_cast<std::byte>((value >> (8U * index)) & 255U); }
}

std::vector<std::byte> oneGlyphSeed(std::span<const std::byte> font, const UI::UITextRasterBatch& batch)
{
    const auto& glyph = batch.glyphs.front();
    const u32 pixels = glyph.width * glyph.height * 4U;
    std::vector<std::byte> bytes(UI::UIBakedFontHeaderBytes + UI::UIBakedGlyphRecordBytes + pixels);
    std::memcpy(bytes.data(), "TMSDFONT", 8);
    writeWord(bytes, 8, UI::UIBakedFontSchemaVersion);
    writeWord(bytes, 12, UI::UIBakedFontHeaderBytes);
    const u64 fingerprint = UI::uiFontFingerprint(font);
    writeWord(bytes, 16, static_cast<u32>(fingerprint));
    writeWord(bytes, 20, static_cast<u32>(fingerprint >> 32U));
    writeWord(bytes, 28, 1);
    writeWord(bytes, 32, pixels);
    writeWord(bytes, 36, UI::UIBakedGlyphRecordBytes);
    writeWord(bytes, 40, UI::UITextMsdfPixelsPerEm);
    writeWord(bytes, 44, std::bit_cast<u32>(UI::UITextMsdfDistanceRange));
    const usize base = UI::UIBakedFontHeaderBytes;
    writeWord(bytes, base, glyph.glyphIndex);
    writeWord(bytes, base + 4, glyph.width);
    writeWord(bytes, base + 8, glyph.height);
    writeWord(bytes, base + 12, glyph.rasterSize.x);
    writeWord(bytes, base + 16, glyph.rasterSize.y);
    writeWord(bytes, base + 20, static_cast<u32>(glyph.imageKind));
    writeWord(bytes, base + 28, pixels);
    writeWord(bytes, base + 32, std::bit_cast<u32>(glyph.bearingX / 24.0F));
    writeWord(bytes, base + 36, std::bit_cast<u32>(glyph.bearingY / 24.0F));
    writeWord(bytes, base + 40, std::bit_cast<u32>(glyph.distanceRange));
    std::memcpy(bytes.data() + base + UI::UIBakedGlyphRecordBytes, batch.coverage.data() + glyph.coverageOffset, pixels);
    return bytes;
}

} // namespace


TEST(FreeTypeTextRasterizerTests, CreateRejectsEmptyFontBytesAndInvalidCapacity)
{
    auto rasterizerResult = UI::createFreeTypeTextRasterizer(
        UI::UITextRasterizerCapacity{.initialFaceCapacity = 1});
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
        UI::UITextRasterizerCapacity{.maxGlyphsPerRaster = 0});
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
            .initialFaceCapacity = 1,
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
    EXPECT_NE(batch->glyphs[0].glyphIndex, 0U);
    EXPECT_NE(batch->glyphs[1].glyphIndex, 0U);
    EXPECT_EQ(batch->glyphs[0].clusterByteBegin, 0U);
    EXPECT_EQ(batch->glyphs[1].clusterByteBegin, 3U);
    EXPECT_EQ(batch->scalars.size(), 2U);
    EXPECT_EQ(batch->missingGlyphCount, 0U);
    EXPECT_EQ(batch->glyphs[0].imageKind, UI::UIGlyphImageKind::Msdf);
    EXPECT_GT(batch->glyphs[0].advance, 0.0F);
    // Source Han should produce non-empty coverage for CJK at 24px.
    EXPECT_GT(batch->glyphs[0].width, 0U);
    EXPECT_GT(batch->glyphs[0].height, 0U);
    EXPECT_FALSE(batch->coverage.empty());
    // Top-left of a glyph box can be transparent; require some ink somewhere.
    bool hasInk = false;
    for (usize index = 0; index + 3U < batch->coverage.size(); index += 4U) {
        std::array<u8, 3> rgb{batch->coverage[index], batch->coverage[index + 1], batch->coverage[index + 2]};
        std::sort(rgb.begin(), rgb.end());
        if (rgb[1] > 127) {
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
            .initialFaceCapacity = 1,
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
    ASSERT_TRUE(context->text().openTextFont(
        std::span<const std::byte>(fontBytes.data(), fontBytes.size())));

    auto rootResult = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(rootResult.has_value())
        << (rootResult ? "" : rootResult.error().message);
    UI::UIRootOwner root = std::move(*rootResult);
    auto updaterResult = context->authoring().treeUpdater(root);
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
    ASSERT_TRUE(context->publication().commitLayout({.width = 200.0F, .height = 80.0F}));

    const UI::UICommittedPaintView paint = context->publication().committedPaint();
    ASSERT_EQ(paint.size(), 2U);
    for (const UI::UICommittedPaintEntry& entry : paint.entries()) {
        EXPECT_EQ(entry.node, label);
        EXPECT_EQ(entry.kind, UI::UICommittedPaintKind::Glyph);
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

    const std::span<const u8> atlas = context->publication().glyphAtlasPixels();
    ASSERT_FALSE(atlas.empty());
    const u32 atlasWidth = context->publication().glyphAtlasWidth();
    ASSERT_GT(atlasWidth, 0U);
    bool hasInk = false;
    for (u32 row = 0; row < paint.entries()[0].atlasHeight && !hasInk; ++row) {
        const usize rowOffset =
            static_cast<usize>(paint.entries()[0].atlasY + row) * atlasWidth
            + paint.entries()[0].atlasX;
        const auto rowPixels = atlas.subspan(rowOffset * 4U, paint.entries()[0].atlasWidth * 4U);
        hasInk = std::any_of(
            rowPixels.begin(), rowPixels.end(), [](u8 coverage) { return coverage != 0; });
    }
    EXPECT_TRUE(hasInk);

    const auto revision = context->publication().glyphAtlasPageRevision();
    const auto rejected = context->text().openTextFont(fontBytes);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidFont);
    EXPECT_EQ(context->publication().glyphAtlasPageRevision(), revision);
}

TEST(FreeTypeTextMeasurementTests, SharesShapedMetricsWithRasterAndRetainedText)
{
    const auto font = loadFontBytes(resolveOptionalFontPath());
    if (font.empty()) { GTEST_SKIP() << "No outline font fixture"; }
    auto windows = WindowPool::Create(1).value();
    auto context = UI::UIContext::Create(windows.tryEmplace(1).value(),
        {.applyDefaultProductChrome = false}, UI::createFreeTypeTextRasterizer().value()).value();
    auto fallback = context->text().measureText("A中😀", {.logicalSize = 15.0F});
    ASSERT_TRUE(fallback);
    EXPECT_FLOAT_EQ(fallback->measuredSize.width, 27.0F);
    EXPECT_FLOAT_EQ(fallback->measuredSize.height, 18.0F);
    ASSERT_TRUE(context->text().openTextFont(font));
    auto reference = UI::createFreeTypeTextRasterizer().value();
    const auto face = reference->openFace(font).value();
    const auto atlasBeforeMeasure = context->publication().glyphAtlasPageRevision();
    const auto wide = context->text().measureText("WWW", {.logicalSize = 15.0F});
    const auto narrow = context->text().measureText("iii", {.logicalSize = 15.0F});
    ASSERT_TRUE(wide);
    ASSERT_TRUE(narrow);
    EXPECT_GT(wide->measuredSize.width, narrow->measuredSize.width);
    EXPECT_EQ(context->publication().glyphAtlasPageRevision(), atlasBeforeMeasure);
    EXPECT_EQ(context->liveNodeCount(), 0U);

    auto root = context->authoring().rootBuilder().createRoot().value();
    auto updater = context->authoring().treeUpdater(root).value();
    UI::UIElementDescriptor descriptor;
    descriptor.text = "";
    const auto label = updater.createElement(root.rootNodeId(), descriptor).value();
    for (const float scale : {1.0F, 1.75F, 2.0F})
    {
        const UI::UITextStyle style{.logicalSize = 15.0F * scale,
                                    .pixelSnap = UI::UITextPixelSnap::RunOrigin};
        ASSERT_TRUE(context->text().setRasterScale({2.0F, 1.5F}));
        for (const std::string_view value : {"AV office", "a\xCC\x81", "中文", " "})
        {
            const auto measured = context->text().measureText(value, style);
            ASSERT_TRUE(measured) << measured.error().message;
            const auto raster = reference->raster(face, value, style, {2.0F, 1.5F});
            ASSERT_TRUE(raster) << raster.error().message;
            EXPECT_EQ(*measured, raster->metrics);
            ASSERT_TRUE(updater.setTextStyle(label, style));
            ASSERT_TRUE(updater.setText(label, value));
            ASSERT_TRUE(context->publication().commitLayout({800.0F, 150.0F}));
            bool found = false;
            for (const auto& entry : context->publication().committedLayout().entries())
            {
                if (entry.node != label) { continue; }
                found = true;
                EXPECT_EQ(entry.contentPlacement.intrinsicSize, measured->measuredSize);
            }
            ASSERT_TRUE(found);
            const auto before = context->statistics();
            const auto atlasRevision = context->publication().glyphAtlasPageRevision();
            EXPECT_TRUE(context->text().measureText("Unpainted glyphs 012345", style));
            EXPECT_EQ(context->statistics().layoutRevision, before.layoutRevision);
            EXPECT_EQ(context->statistics().paintRevision, before.paintRevision);
            EXPECT_EQ(context->statistics().layoutDirty, before.layoutDirty);
            EXPECT_EQ(context->statistics().paintDirty, before.paintDirty);
            EXPECT_EQ(context->publication().glyphAtlasPageRevision(), atlasRevision);
        }
    }
}

TEST(FreeTypeTextMeasurementTests, UsesTheConfiguredFallbackChain)
{
    const auto latin = systemTestFont("arial.ttf");
    const auto cjk = loadFontBytes(resolveOptionalFontPath());
    if (latin.empty() || cjk.empty()) { GTEST_SKIP() << "No Latin/CJK fallback fixtures"; }
    auto reference = UI::createFreeTypeTextRasterizer().value();
    const auto primary = reference->openFace(latin).value();
    const auto fallback = reference->openFace(cjk).value();
    const std::array chain{fallback};
    ASSERT_TRUE(reference->setFallbackChain(chain));
    auto windows = WindowPool::Create(1).value();
    auto context = UI::UIContext::Create(windows.tryEmplace(1).value(),
        {.applyDefaultProductChrome = false}, UI::createFreeTypeTextRasterizer().value()).value();
    ASSERT_TRUE(context->text().openTextFont(latin));
    ASSERT_TRUE(context->text().addFallbackFont(cjk));
    const UI::UITextStyle style{.logicalSize = 19.5F};
    const auto measured = context->text().measureText("AV 中文", style);
    const auto raster = reference->raster(primary, "AV 中文", style);
    ASSERT_TRUE(measured);
    ASSERT_TRUE(raster);
    EXPECT_EQ(*measured, raster->metrics);
    EXPECT_EQ(raster->missingGlyphCount, 0U);
    EXPECT_TRUE(std::any_of(raster->glyphs.begin(), raster->glyphs.end(),
                           [&](const auto& glyph) { return glyph.face == fallback; }));
    EXPECT_EQ(context->liveNodeCount(), 0U);
    EXPECT_TRUE(context->publication().committedPaint().empty());
}

TEST(FreeTypeTextMeasurementTests, ShapeAndLongMultilineMeasureIgnoreImageBudgets)
{
    const auto font = loadFontBytes(resolveOptionalFontPath());
    if (font.empty()) { GTEST_SKIP() << "No outline font fixture"; }
    auto rasterizer = UI::createFreeTypeTextRasterizer(
        {.initialFaceCapacity = 0, .maxGlyphsPerRaster = 1, .coverageByteCapacity = 4}).value();
    const auto face = rasterizer->openFace(font).value();
    const std::string text = "AV\n" + std::string(5000, '\n');
    const auto shaped = rasterizer->shape(face, text, {});
    ASSERT_TRUE(shaped);
    EXPECT_EQ(shaped->metrics.codepointCount, text.size());
    EXPECT_EQ(shaped->scalars.size(), 2U);
    UI::Detail::UITextLineLayout layout(*std::pmr::get_default_resource());
    auto measured = layout.measure(text, {}, rasterizer.get(), face, {100, UI::UITextWrapMode::Words});
    ASSERT_TRUE(measured);
    EXPECT_EQ(measured->lineCount, 5002U);
    EXPECT_EQ(measured->codepointCount, text.size());
    auto raster = rasterizer->raster(face, "AV", {});
    ASSERT_FALSE(raster);
    EXPECT_EQ(raster.error().code, UI::UIErrorCode::CapacityExceeded);
}

TEST(FreeTypeTextMeasurementTests, FontSlotsGrowFromZeroAndKeepStableGenerations)
{
    const auto font = systemTestFont("NotoSans-Regular.ttf");
    if (font.empty()) { GTEST_SKIP() << "No Noto Sans fixture"; }
    auto rasterizer = UI::createFreeTypeTextRasterizer({.initialFaceCapacity = 0}).value();
    std::vector<UI::UIFontFaceId> faces;
    for (usize index = 0; index < 66; ++index)
    {
        auto face = rasterizer->openFace(font);
        ASSERT_TRUE(face);
        faces.push_back(*face);
    }
    ASSERT_TRUE(rasterizer->setFallbackChain(faces));
    ASSERT_TRUE(rasterizer->closeFace(faces[32]));
    EXPECT_FALSE(rasterizer->shape(faces[32], "A", {}));
    const auto replacement = rasterizer->openFace(font).value();
    EXPECT_EQ(replacement.index, faces[32].index);
    EXPECT_NE(replacement.generation, faces[32].generation);
    EXPECT_TRUE(rasterizer->shape(faces.front(), "office", {}));
    const std::array duplicate{replacement, replacement};
    EXPECT_FALSE(rasterizer->setFallbackChain(duplicate));
}

TEST(FreeTypeTextMeasurementTests, EllipsisPreservesShapingClustersAsWellAsGraphemes)
{
    const auto font = systemTestFont("NotoSans-Regular.ttf");
    if (font.empty()) { GTEST_SKIP() << "No Noto Sans shaping fixture"; }
    auto rasterizer = UI::createFreeTypeTextRasterizer().value();
    const auto face = rasterizer->openFace(font).value();
    const UI::UITextStyle style{.logicalSize = 24};
    const auto shaped = rasterizer->shape(face, "office", style);
    ASSERT_TRUE(shaped);
    ASSERT_EQ(shaped->scalars.size(), 6U);
    const float fullWidth = shaped->metrics.measuredSize.width;
    std::vector<usize> boundaries{0};
    for (const auto& scalar : shaped->scalars) { boundaries.push_back(scalar.clusterByteEnd); }
    const auto markerWidth = rasterizer->measure(face, UI::UITextEllipsisUtf8, style)->measuredSize.width;
    UI::Detail::UITextTruncationScratch scratch(*std::pmr::get_default_resource());
    for (float width = markerWidth + 1; width < fullWidth; width += 1)
    {
        const auto plan = UI::Detail::resolveTextTruncation(
            scratch, {.rasterizer = rasterizer.get(), .face = face}, "office", style,
            UI::UITextOverflow::Ellipsis, width, fullWidth);
        ASSERT_TRUE(plan);
        EXPECT_TRUE(plan->showEllipsis);
        EXPECT_NE(std::find(boundaries.begin(), boundaries.end(), plan->visibleText.size()), boundaries.end());
        const auto prefix = rasterizer->measure(face, plan->visibleText, style);
        ASSERT_TRUE(prefix);
        EXPECT_LE(prefix->measuredSize.width + markerWidth, width + 0.001F);
    }
}

TEST(FreeTypeTextMeasurementTests, WrappedFallbackAndRtlMeasurementsMatchThePaintedSlices)
{
    const auto latin = systemTestFont("NotoSans-Regular.ttf");
    const auto arabic = systemTestFont("NotoNaskhArabic-Regular.ttf");
    const auto cjk = loadFontBytes(resolveOptionalFontPath());
    if (latin.empty() || arabic.empty() || cjk.empty()) { GTEST_SKIP() << "No complete fallback fixtures"; }
    auto rasterizer = UI::createFreeTypeTextRasterizer().value();
    const auto face = rasterizer->openFace(latin).value();
    const std::array fallbacks{rasterizer->openFace(arabic).value(), rasterizer->openFace(cjk).value()};
    ASSERT_TRUE(rasterizer->setFallbackChain(fallbacks));
    const UI::UITextStyle style{.logicalSize = 17.5F};
    const UI::UITextMeasureOptions options{60, UI::UITextWrapMode::Words, {2}};
    UI::Detail::UITextLineLayout layout(*std::pmr::get_default_resource());
    for (const std::string_view text : {"office 中文 office 中文", "123 سلام سلام سلام", "AV\n中文 سلام office"})
    {
        const auto measured = layout.measure(text, style, rasterizer.get(), face, options);
        ASSERT_TRUE(measured);
        const auto lines = layout.build(text, style, rasterizer.get(), face, options);
        ASSERT_TRUE(lines);
        float maximumWidth = 0;
        for (const auto& line : *lines)
        {
            auto lineStyle = style;
            lineStyle.direction = line.rightToLeft ? UI::UITextDirection::RightToLeft : UI::UITextDirection::LeftToRight;
            const auto batch = rasterizer->raster(face, text.substr(line.byteBegin, line.byteEnd - line.byteBegin), lineStyle);
            ASSERT_TRUE(batch);
            EXPECT_EQ(batch->missingGlyphCount, 0U);
            float width = batch->metrics.measuredSize.width;
            if (line.showEllipsis)
            {
                const auto marker = rasterizer->raster(face, UI::UITextEllipsisUtf8, lineStyle);
                ASSERT_TRUE(marker);
                width += marker->metrics.measuredSize.width;
            }
            maximumWidth = (std::max)(maximumWidth, width);
        }
        EXPECT_FLOAT_EQ(measured->measuredSize.width, maximumWidth);
        EXPECT_EQ(measured->lineCount, lines->size());
        EXPECT_FLOAT_EQ(measured->measuredSize.height, lines->size() * style.logicalSize * style.lineHeightScale);
    }
}

TEST(FreeTypeTextRasterizerTests, MsdfPixelsAreSharedAcrossFontSizesAndAnisotropicDpi)
{
    const auto font = loadFontBytes(resolveOptionalFontPath());
    if (font.empty()) { GTEST_SKIP() << "No outline font fixture"; }
    auto rasterizer = UI::createFreeTypeTextRasterizer().value();
    const auto face = rasterizer->openFace(font).value();
    UI::UITextStyle style{.logicalSize = 24.0F};
    auto first = rasterizer->raster(face, "Ag中文", style);
    ASSERT_TRUE(first) << first.error().message;
    const auto firstGlyph = first->glyphs.front();
    const auto firstMetrics = first->metrics;
    const std::vector<u8> pixels(first->coverage.begin(), first->coverage.end());
    auto highDpi = rasterizer->raster(face, "Ag中文", style, {2.0F, 1.5F});
    ASSERT_TRUE(highDpi);
    EXPECT_EQ(std::vector<u8>(highDpi->coverage.begin(), highDpi->coverage.end()), pixels);
    EXPECT_EQ(highDpi->metrics, firstMetrics);
    EXPECT_EQ(highDpi->glyphs.front().rasterSize, firstGlyph.rasterSize);
    EXPECT_FLOAT_EQ(highDpi->glyphs.front().logicalWidth, firstGlyph.logicalWidth);
    style.logicalSize = 48.0F;
    auto large = rasterizer->raster(face, "Ag中文", style);
    ASSERT_TRUE(large);
    EXPECT_EQ(std::vector<u8>(large->coverage.begin(), large->coverage.end()), pixels);
    EXPECT_FLOAT_EQ(large->glyphs.front().logicalWidth, 2.0F * firstGlyph.logicalWidth);
    EXPECT_FLOAT_EQ(large->metrics.measuredSize.width, 2.0F * firstMetrics.measuredSize.width);
}

TEST(TextShaperTests, LatinLigaturesAndCombiningMarksKeepLogicalScalarClusters)
{
    auto font = systemTestFont("NotoSans-Regular.ttf");
    if (font.empty()) { GTEST_SKIP() << "No Noto Sans shaping fixture"; }
    auto shaper = UI::TextShaper::Create().value();
    const auto face = shaper->openFace(font).value();
    font.clear(); // The shaper owns its own immutable source bytes.
    auto run = shaper->shape(face, "office", {});
    ASSERT_TRUE(run) << run.error().message;
    EXPECT_EQ(run->scalars.size(), 6U);
    EXPECT_LT(run->glyphs.size(), run->scalars.size());
    EXPECT_EQ(run->missingGlyphCount, 0U);
    const auto count = run->glyphs.size();
    for (int index = 0; index < 20; ++index) { EXPECT_EQ(shaper->shape(face, "office", {})->glyphs.size(), count); }
    auto mark = shaper->shape(face, "a\xCC\x81", {});
    ASSERT_TRUE(mark);
    ASSERT_EQ(mark->scalars.size(), 2U);
    EXPECT_EQ(mark->scalars[0].clusterByteBegin, mark->scalars[1].clusterByteBegin);
    EXPECT_EQ(mark->scalars[0].clusterByteEnd, 3U);
    EXPECT_FALSE(shaper->shape(face, "\xC0\xAF", {}));
}

TEST(FreeTypeTextRasterizerTests, AsciiAndPunctuationSaturateUnassignedDistanceChannels)
{
    const auto font = loadFontBytes(resolveOptionalFontPath());
    if (font.empty()) { GTEST_SKIP() << "No outline font fixture"; }
    auto rasterizer = UI::createFreeTypeTextRasterizer().value();
    const auto face = rasterizer->openFace(font).value();
    const auto batch = rasterizer->raster(face,
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,:;!?+-*/()[]{}_%#@&=<>…", {});
    ASSERT_TRUE(batch) << batch.error().message;
    EXPECT_EQ(batch->missingGlyphCount, 0U);
    EXPECT_FALSE(batch->coverage.empty());
}

TEST(FreeTypeTextRasterizerTests, CffAndTrueTypeContoursHaveTransparentExteriorAndOpaqueInk)
{
    const std::array fonts{loadFontBytes(resolveOptionalFontPath()), systemTestFont("arial.ttf")};
    usize tested = 0;
    for (const auto& font : fonts)
    {
        if (font.empty()) { continue; }
        ++tested;
        auto rasterizer = UI::createFreeTypeTextRasterizer().value();
        const auto face = rasterizer->openFace(font).value();
        auto batch = rasterizer->raster(face, "OAg", {});
        ASSERT_TRUE(batch) << batch.error().message;
        const auto median = [&](usize pixel) {
            std::array<u8, 3> rgb{batch->coverage[pixel], batch->coverage[pixel + 1U], batch->coverage[pixel + 2U]};
            std::sort(rgb.begin(), rgb.end());
            return rgb[1];
        };
        for (const auto& glyph : batch->glyphs)
        {
            ASSERT_EQ(glyph.imageKind, UI::UIGlyphImageKind::Msdf);
            ASSERT_GT(glyph.width, 2U);
            ASSERT_GT(glyph.height, 2U);
            EXPECT_LT(median(glyph.coverageOffset), 128U);
            EXPECT_LT(median(glyph.coverageOffset + (glyph.width * glyph.height - 1U) * 4U), 128U);
            bool ink = false;
            for (u32 pixel = 0; pixel < glyph.width * glyph.height; ++pixel)
            { ink = ink || median(glyph.coverageOffset + pixel * 4U) > 127U; }
            EXPECT_TRUE(ink);
        }
    }
    if (tested == 0) { GTEST_SKIP() << "No outline fixtures"; }
}

TEST(TextShaperTests, ArabicUsesContextualGlyphsAndResolvedParagraphDirectionAfterDigits)
{
    const auto font = systemTestFont("NotoNaskhArabic-Regular.ttf");
    if (font.empty()) { GTEST_SKIP() << "No Arabic fixture"; }
    auto shaper = UI::TextShaper::Create().value();
    const auto face = shaper->openFace(font).value();
    auto isolated = shaper->shape(face, "س", {});
    ASSERT_TRUE(isolated);
    const u32 isolatedSeen = isolated->glyphs.front().glyphIndex;
    auto joined = shaper->shape(face, "سلام", {});
    ASSERT_TRUE(joined);
    ASSERT_EQ(joined->scalars.size(), 4U);
    EXPECT_EQ(joined->missingGlyphCount, 0U);
    EXPECT_TRUE(joined->scalars.front().paragraphRightToLeft);
    EXPECT_GT(joined->scalars.front().visualStartX, joined->scalars.front().visualEndX);
    const auto seen = std::find_if(joined->glyphs.begin(), joined->glyphs.end(), [](const auto& glyph) { return glyph.clusterByteBegin == 0; });
    ASSERT_NE(seen, joined->glyphs.end());
    EXPECT_NE(seen->glyphIndex, isolatedSeen);
    auto mixed = shaper->shape(face, "123 سلام", {});
    ASSERT_TRUE(mixed);
    EXPECT_TRUE(mixed->scalars.front().paragraphRightToLeft);
    EXPECT_FALSE(mixed->scalars.front().rightToLeft);
}

TEST(TextShaperTests, FallbackSelectsWholeScriptClustersAndMissingGlyphsAreCounted)
{
    const auto latin = systemTestFont("NotoSans-Regular.ttf");
    const auto cjk = loadFontBytes(resolveOptionalFontPath());
    const auto arabic = systemTestFont("NotoNaskhArabic-Regular.ttf");
    if (latin.empty() || cjk.empty() || arabic.empty()) { GTEST_SKIP() << "No complete fallback fixture chain"; }
    auto shaper = UI::TextShaper::Create().value();
    const auto primary = shaper->openFace(latin).value();
    const auto cjkFace = shaper->openFace(cjk).value();
    const auto arabicFace = shaper->openFace(arabic).value();
    const std::array chain{cjkFace, arabicFace};
    ASSERT_TRUE(shaper->setFallbackChain(chain));
    auto run = shaper->shape(primary, "A中سلام", {});
    ASSERT_TRUE(run);
    EXPECT_EQ(run->missingGlyphCount, 0U);
    EXPECT_TRUE(std::any_of(run->glyphs.begin(), run->glyphs.end(), [&](const auto& glyph) { return glyph.face == cjkFace; }));
    EXPECT_TRUE(std::any_of(run->glyphs.begin(), run->glyphs.end(), [&](const auto& glyph) { return glyph.face == arabicFace; }));
    const std::array invalidChain{UI::UIFontFaceId{99, 99}};
    EXPECT_FALSE(shaper->setFallbackChain(invalidChain));
    EXPECT_EQ(shaper->shape(primary, "中", {})->missingGlyphCount, 0U);
    auto missing = shaper->shape(primary, "\xF4\x8F\xBF\xBF", {}); // U+10FFFF
    ASSERT_TRUE(missing);
    EXPECT_GT(missing->missingGlyphCount, 0U);
}

TEST(TextShaperTests, DevanagariAndThaiAreShapedWithoutMissingMarks)
{
    const auto indic = systemTestFont("Nirmala.ttc");
    const auto thai = systemTestFont("tahoma.ttf");
    if (indic.empty() || thai.empty()) { GTEST_SKIP() << "No Indic/Thai system fixtures"; }
    auto shaper = UI::TextShaper::Create().value();
    const auto indicFace = shaper->openFace(indic).value();
    const auto thaiFace = shaper->openFace(thai).value();
    auto syllables = shaper->shape(indicFace, "कि क्ष", {});
    ASSERT_TRUE(syllables);
    EXPECT_EQ(syllables->missingGlyphCount, 0U);
    ASSERT_GE(syllables->scalars.size(), 2U);
    EXPECT_EQ(syllables->scalars[0].clusterByteBegin, syllables->scalars[1].clusterByteBegin);
    auto marks = shaper->shape(thaiFace, "เก้า", {});
    ASSERT_TRUE(marks);
    EXPECT_EQ(marks->missingGlyphCount, 0U);
    EXPECT_TRUE(std::any_of(marks->glyphs.begin(), marks->glyphs.end(), [](const auto& glyph) {
        return glyph.offsetX != 0.0F || glyph.offsetY != 0.0F || glyph.advanceX == 0.0F; }));
}

TEST(FreeTypeTextRasterizerTests, ColorEmojiFallbackPreservesZwjLigatureAndPremultipliedPixels)
{
    const auto primaryBytes = systemTestFont("NotoSans-Regular.ttf");
    const auto emojiBytes = systemTestFont("seguiemj.ttf");
    if (primaryBytes.empty() || emojiBytes.empty()) { GTEST_SKIP() << "No color Emoji fixture"; }
    auto rasterizer = UI::createFreeTypeTextRasterizer().value();
    const auto primary = rasterizer->openFace(primaryBytes).value();
    const auto emoji = rasterizer->openFace(emojiBytes).value();
    const std::array chain{emoji};
    ASSERT_TRUE(rasterizer->setFallbackChain(chain));
    auto batch = rasterizer->raster(primary, "😀👩‍💻", {.logicalSize = 32.0F});
    ASSERT_TRUE(batch) << batch.error().message;
    EXPECT_EQ(batch->missingGlyphCount, 0U);
    ASSERT_EQ(batch->glyphs.size(), 2U);
    EXPECT_EQ(batch->glyphs[1].clusterByteBegin, 4U);
    bool hasColor = false;
    for (const auto& glyph : batch->glyphs)
    {
        EXPECT_EQ(glyph.face, emoji);
        EXPECT_EQ(glyph.imageKind, UI::UIGlyphImageKind::Color);
    }
    for (usize index = 0; index + 3U < batch->coverage.size(); index += 4U)
    {
        hasColor = hasColor || batch->coverage[index] != batch->coverage[index + 1U];
        EXPECT_LE(batch->coverage[index], batch->coverage[index + 3U]);
        EXPECT_LE(batch->coverage[index + 1U], batch->coverage[index + 3U]);
        EXPECT_LE(batch->coverage[index + 2U], batch->coverage[index + 3U]);
    }
    EXPECT_TRUE(hasColor);
}

TEST(FreeTypeTextRasterizerTests, BakedSeedRoundTripsAndRejectsStaleOrTruncatedData)
{
    const auto font = loadFontBytes(resolveOptionalFontPath());
    if (font.empty()) { GTEST_SKIP() << "No outline font fixture"; }
    auto source = UI::createFreeTypeTextRasterizer().value();
    const auto face = source->openFace(font).value();
    auto batch = source->raster(face, "A", {.logicalSize = 24.0F});
    ASSERT_TRUE(batch);
    auto seed = oneGlyphSeed(font, *batch);
    auto view = UI::parseUIBakedFont(seed);
    ASSERT_TRUE(view);
    EXPECT_EQ(view->glyphCount, 1U);
    EXPECT_EQ(view->glyph(0).glyphIndex, batch->glyphs[0].glyphIndex);
    auto destination = UI::createFreeTypeTextRasterizer({.glyphImageCapacity = 1}).value();
    const auto target = destination->openFace(font).value();
    ASSERT_TRUE(destination->primeGlyphCache(target, seed));
    auto restored = destination->raster(target, "A", {.logicalSize = 24.0F});
    ASSERT_TRUE(restored);
    EXPECT_EQ(std::vector<u8>(restored->coverage.begin(), restored->coverage.end()),
              std::vector<u8>(view->pixels.begin(), view->pixels.end()));
    EXPECT_FALSE(destination->raster(target, "B", {}));
    seed[16] ^= std::byte{1};
    EXPECT_FALSE(destination->primeGlyphCache(target, seed));
    EXPECT_TRUE(destination->raster(target, "A", {}));
    seed.pop_back();
    EXPECT_FALSE(UI::parseUIBakedFont(seed));
}

} // namespace Tina::Tests
