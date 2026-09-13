#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UIAuthoring.hpp>
#include <tina/ui/UIContext.hpp>
#include <tina/ui/UIErrors.hpp>
#include "detail/UITextWrapping.hpp"
#include <tina/ui/UIPublicationPipeline.hpp>
#include <tina/ui/UITextSystem.hpp>
#include <tina/ui/text/UITextRasterizer.hpp>

#include <memory>
#include <memory_resource>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <vector>
#include <thread>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

class UITextMeasurementTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto pool = WindowPool::Create(1);
        ASSERT_TRUE(pool);
        windows.emplace(std::move(*pool));
        auto window = windows->tryEmplace(1);
        ASSERT_TRUE(window);
        auto created = UI::UIContext::Create(*window, {.applyDefaultProductChrome = false});
        ASSERT_TRUE(created);
        context = std::move(*created);
    }
    std::optional<WindowPool> windows;
    std::unique_ptr<UI::UIContext> context;
};

} // namespace

TEST(UITextRasterizerTests, PlaceholderOpenMeasureRasterAndClose)
{
    auto rasterizerResult = UI::createPlaceholderTextRasterizer(
        UI::UITextRasterizerCapacity{
            .initialFaceCapacity = 2,
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
    EXPECT_EQ(batch->glyphs[0].glyphIndex, static_cast<u32>('A'));
    EXPECT_EQ(batch->glyphs[1].glyphIndex, static_cast<u32>('B'));
    EXPECT_GT(batch->glyphs[0].width, 0U);
    EXPECT_GT(batch->glyphs[0].height, 0U);
    EXPECT_EQ(
        batch->coverage.size(),
        (static_cast<usize>(batch->glyphs[0].width) * batch->glyphs[0].height
            + static_cast<usize>(batch->glyphs[1].width) * batch->glyphs[1].height) * 4U);
    EXPECT_EQ(batch->scalars.size(), 2U);
    EXPECT_EQ(batch->glyphs[0].coveragePitch, batch->glyphs[0].width * 4U);
    EXPECT_EQ(batch->coverage.front(), 255);

    auto cjk = rasterizer->raster(face, "中", {});
    ASSERT_TRUE(cjk.has_value()) << (cjk ? "" : cjk.error().message);
    ASSERT_EQ(cjk->glyphs.size(), 1U);
    EXPECT_EQ(cjk->glyphs[0].glyphIndex, 0x4E2DU);

    assertOk(rasterizer->closeFace(face));
    auto closed = rasterizer->measure(face, "A", {});
    ASSERT_FALSE(closed.has_value());
    EXPECT_EQ(closed.error().code, UI::UIErrorCode::InvalidFont);
}

TEST(UITextRasterizerTests, PlaceholderRejectsFontBytesButGrowsBeyondInitialReservation)
{
    auto rasterizerResult = UI::createPlaceholderTextRasterizer(
        UI::UITextRasterizerCapacity{.initialFaceCapacity = 1});
    ASSERT_TRUE(rasterizerResult.has_value());
    std::unique_ptr<UI::IUITextRasterizer> rasterizer = std::move(*rasterizerResult);

    const std::byte blob[1]{std::byte{0}};
    auto nonEmpty = rasterizer->openFace(std::span<const std::byte>(blob, 1));
    ASSERT_FALSE(nonEmpty.has_value());
    EXPECT_EQ(nonEmpty.error().code, UI::UIErrorCode::InvalidFont);

    auto first = rasterizer->openFace({});
    ASSERT_TRUE(first.has_value());
    auto second = rasterizer->openFace({});
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*first, *second);
    assertOk(rasterizer->closeFace(*first));
    auto reused = rasterizer->openFace({});
    ASSERT_TRUE(reused);
    EXPECT_EQ(reused->index, first->index);
    EXPECT_NE(reused->generation, first->generation);
    EXPECT_FALSE(rasterizer->shape(*first, "A", {}));

    auto invalidCapacity = UI::createPlaceholderTextRasterizer(
        UI::UITextRasterizerCapacity{.maxGlyphsPerRaster = 0});
    ASSERT_FALSE(invalidCapacity.has_value());
    EXPECT_EQ(invalidCapacity.error().code, UI::UIErrorCode::InvalidContextConfig);
}

TEST(UITextRasterizerTests, LazyFacesGrowBeyondSixtyFourAndFallbackRejectsDuplicates)
{
    auto rasterizer = UI::createPlaceholderTextRasterizer({.initialFaceCapacity = 0}).value();
    std::vector<UI::UIFontFaceId> faces;
    for (usize index = 0; index < 70; ++index)
    {
        auto face = rasterizer->openFace({});
        ASSERT_TRUE(face);
        faces.push_back(*face);
    }
    EXPECT_TRUE(rasterizer->setFallbackChain(faces));
    faces.push_back(faces.front());
    EXPECT_FALSE(rasterizer->setFallbackChain(faces));
    EXPECT_TRUE(rasterizer->measure(faces.front(), "仍然有效", {}));
}

TEST(UITextRasterizerTests, ShapeAndWrappedMeasureDoNotConsumeRasterBudgets)
{
    auto rasterizer = UI::createPlaceholderTextRasterizer(
        {.maxGlyphsPerRaster = 1, .coverageByteCapacity = 4}).value();
    const auto face = rasterizer->openFace({}).value();
    std::string text;
    for (usize index = 0; index < 5000; ++index) { text += "A\n"; }
    const auto shaped = rasterizer->shape(face, text, {});
    ASSERT_TRUE(shaped);
    EXPECT_EQ(shaped->scalars.size(), 5000U);
    EXPECT_EQ(shaped->metrics.codepointCount, 10000U);
    UI::Detail::UITextLineLayout layout{*std::pmr::get_default_resource()};
    const auto measured = layout.measure(text, {}, rasterizer.get(), face, {10.0F, UI::UITextWrapMode::Words});
    ASSERT_TRUE(measured);
    EXPECT_EQ(measured->lineCount, 5001U);
    const auto rendered = rasterizer->raster(face, "AB", {});
    ASSERT_FALSE(rendered);
    EXPECT_EQ(rendered.error().code, UI::UIErrorCode::CapacityExceeded);
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

TEST(UITextRasterizerTests, ContextCreateWiresPlaceholderRasterizerForTextMeasure)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    auto contextResult = UI::UIContext::Create(*windowResult, UI::UIContextCapacityConfig{.applyDefaultProductChrome = false});
    ASSERT_TRUE(contextResult.has_value())
        << (contextResult ? "" : contextResult.error().message);
    auto& context = **contextResult;
    auto root = *context.authoring().rootBuilder().createRoot();
    auto updater = *context.authoring().treeUpdater(root);
    auto label = *updater.createElement(root.rootNodeId(), UI::makeLabelElement());

    assertOk(updater.setText(label, "Hi"));
    auto text = updater.text(label);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Hi");
    assertOk(context.publication().commitLayout(UI::UILogicalSize{.width = 200.0F, .height = 100.0F}));
    EXPECT_FALSE(context.publication().committedLayout().empty());
}

TEST(UITextRasterizerTests, ContextCreateRejectsNullRasterizer)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    auto result = UI::UIContext::Create(
        *windowResult,
        UI::UIContextCapacityConfig{.applyDefaultProductChrome = false},
        std::unique_ptr<UI::IUITextRasterizer>{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, UI::UIErrorCode::InvalidFont);
}

TEST_F(UITextMeasurementTest, MeasuresWithoutNodesAndCountsUnicodeScalars)
{
    const auto text = context->text();
    const UI::UITextStyle style{.logicalSize = 15.0F};
    const auto before = context->statistics();
    auto measured = text.measureText("A中😀", style);
    ASSERT_TRUE(measured);
    EXPECT_EQ(measured->codepointCount, 3U);
    EXPECT_EQ(measured->lineCount, 1U);
    EXPECT_FLOAT_EQ(measured->measuredSize.width, 27.0F);
    EXPECT_FLOAT_EQ(measured->measuredSize.height, 18.0F);
    const auto saved = *measured;
    auto empty = text.measureText({}, style);
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->measuredSize, UI::UILogicalSize{});
    EXPECT_EQ(empty->codepointCount, 0U);
    EXPECT_EQ(*measured, saved);
    const auto after = context->statistics();
    EXPECT_EQ(after.liveNodeCount, 0U);
    EXPECT_EQ(after.liveRootCount, 0U);
    EXPECT_EQ(after.textByteUsed, before.textByteUsed);
    EXPECT_EQ(after.dirtyQueuePendingCount, before.dirtyQueuePendingCount);
    EXPECT_EQ(after.layoutRevision, before.layoutRevision);
    EXPECT_EQ(after.paintRevision, before.paintRevision);
}

TEST_F(UITextMeasurementTest, MatchesCommittedTextWithoutDirtyingPublishedState)
{
    auto root = context->authoring().rootBuilder().createRoot();
    ASSERT_TRUE(root);
    auto updater = context->authoring().treeUpdater(*root);
    ASSERT_TRUE(updater);
    UI::UIElementDescriptor descriptor;
    descriptor.text = "HP 5 COINS 100";
    descriptor.textStyle = UI::UITextStyle{.logicalSize = 15.0F};
    auto label = updater->createElement(root->rootNodeId(), descriptor);
    ASSERT_TRUE(label);
    auto measured = context->text().measureText(*descriptor.text, *descriptor.textStyle);
    ASSERT_TRUE(measured);
    ASSERT_TRUE(context->publication().commitLayout({400.0F, 100.0F}));
    const auto layout = context->publication().committedLayout();
    bool found = false;
    for (const auto& entry : layout.entries())
    {
        if (entry.node != *label) { continue; }
        found = true;
        EXPECT_TRUE(entry.contentPlacement.hasIntrinsicContent);
        EXPECT_EQ(entry.contentPlacement.intrinsicSize, measured->measuredSize);
    }
    ASSERT_TRUE(found);
    const auto before = context->statistics();
    const auto atlasRevision = context->publication().glyphAtlasPageRevision();
    ASSERT_TRUE(context->text().measureText("A different string", {.logicalSize = 21.5F}));
    const auto after = context->statistics();
    EXPECT_EQ(after.liveNodeCount, before.liveNodeCount);
    EXPECT_EQ(after.textByteUsed, before.textByteUsed);
    EXPECT_EQ(after.layoutRevision, before.layoutRevision);
    EXPECT_EQ(after.paintRevision, before.paintRevision);
    EXPECT_EQ(after.structureDirty, before.structureDirty);
    EXPECT_EQ(after.layoutDirty, before.layoutDirty);
    EXPECT_EQ(after.paintDirty, before.paintDirty);
    EXPECT_EQ(after.dirtyQueuePendingCount, before.dirtyQueuePendingCount);
    EXPECT_EQ(context->publication().committedLayout().entries().data(), layout.entries().data());
    EXPECT_EQ(context->publication().glyphAtlasPageRevision(), atlasRevision);
}

TEST_F(UITextMeasurementTest, RejectsInvalidInputAndCrossThreadAccess)
{
    const auto text = context->text();
    for (const float size : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()})
    {
        auto result = text.measureText("A", {.logicalSize = size});
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, UI::UIErrorCode::InvalidText);
    }
    for (const std::string_view invalid : {std::string_view{"\xC0\xAF"},
                                           std::string_view{"A\0B", 3},
                                           std::string_view{"\xF0\x9F"}})
    {
        auto result = text.measureText(invalid, {});
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, UI::UIErrorCode::InvalidText);
    }
    std::optional<Core::Result<UI::UITextMetrics>> crossThread;
    std::thread worker([&] { crossThread.emplace(text.measureText("A", {})); });
    worker.join();
    ASSERT_TRUE(crossThread);
    ASSERT_FALSE(*crossThread);
    EXPECT_EQ(crossThread->error().code, UI::UIErrorCode::WrongOwnerThread);
    EXPECT_TRUE(text.measureText("A", {}));
}

TEST_F(UITextMeasurementTest, ConstrainedMeasurementMatchesAutoHeightWithPaddingAndClamp)
{
    const UI::UITextStyle style{.logicalSize = 10.0F, .advanceScale = 0.5F, .lineHeightScale = 1.5F};
    constexpr std::string_view text = "AB CD EF GH";
    const UI::UITextMeasureOptions options{15.0F, UI::UITextWrapMode::Words, {2}};
    auto measured = context->text().measureText(text, style, options);
    ASSERT_TRUE(measured);
    EXPECT_EQ(measured->lineCount, 2U);
    EXPECT_EQ(measured->codepointCount, text.size());
    auto root = context->authoring().rootBuilder().createRoot().value();
    auto updater = context->authoring().treeUpdater(root).value();
    auto descriptor = UI::makeLabelElement();
    descriptor.text = text;
    descriptor.textStyle = style;
    descriptor.textLineClamp = options.lineClamp;
    descriptor.layout.size.width = UI::UILayoutLength::Px(35.0F);
    descriptor.layout.padding = {10.0F, 3.0F, 10.0F, 3.0F};
    auto node = updater.createElement(root.rootNodeId(), descriptor);
    ASSERT_TRUE(node);
    ASSERT_TRUE(context->publication().commitLayout({400.0F, 200.0F}));
    bool found = false;
    for (const auto& entry : context->publication().committedLayout().entries())
    {
        if (entry.node != *node) { continue; }
        found = true;
        EXPECT_EQ(entry.contentPlacement.intrinsicSize, measured->measuredSize);
        EXPECT_FLOAT_EQ(entry.worldRect.height, measured->measuredSize.height + 6.0F);
    }
    EXPECT_TRUE(found);
    const auto before = context->statistics();
    const auto atlasRevision = context->publication().glyphAtlasPageRevision();
    ASSERT_TRUE(context->text().measureText("不同内容", style, options));
    EXPECT_EQ(context->statistics().paintRevision, before.paintRevision);
    EXPECT_EQ(context->publication().glyphAtlasPageRevision(), atlasRevision);
}

TEST_F(UITextMeasurementTest, ZeroWidthAndInvalidOptionsAreNotUnlimitedWidth)
{
    const UI::UITextStyle style{.logicalSize = 10.0F};
    const auto zero = context->text().measureText("ABC", style, {0.0F, UI::UITextWrapMode::Words});
    ASSERT_TRUE(zero);
    EXPECT_EQ(zero->lineCount, 3U);
    EXPECT_FALSE(context->text().measureText("A", style, {-1.0F}));
    EXPECT_FALSE(context->text().measureText("A", style, {std::numeric_limits<float>::quiet_NaN()}));
    EXPECT_FALSE(context->text().measureText("A", style, {10.0F, UI::UITextWrapMode::NoWrap, {1}}));
    EXPECT_FALSE(context->text().measureText("A", style, {10.0F, static_cast<UI::UITextWrapMode>(255)}));
}

} // namespace Tina::Tests
