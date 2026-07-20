#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {})
{
    auto result = UI::UIContext::Create(window, capacities);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(
    UI::UIContext& context,
    UI::UIRootOwner& root)
{
    auto result = context.treeUpdater(root);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UITreeUpdater{};
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

} // namespace

TEST(UITextTests, MeasurePlaceholderTextIsDeterministicForLatinAndCjk)
{
    const auto latin = UI::measurePlaceholderText("AB", {});
    ASSERT_TRUE(latin.has_value()) << (latin ? "" : latin.error().message);
    EXPECT_EQ(latin->codepointCount, 2U);
    EXPECT_EQ(latin->lineCount, 1U);
    EXPECT_FLOAT_EQ(latin->measuredSize.width, 16.0F * 0.6F * 2.0F);
    EXPECT_FLOAT_EQ(latin->measuredSize.height, 16.0F * 1.2F);

    // Source files are UTF-8; use char literals (not char8_t) for string_view APIs.
    const auto cjk = UI::measurePlaceholderText("中文", {});
    ASSERT_TRUE(cjk.has_value()) << (cjk ? "" : cjk.error().message);
    EXPECT_EQ(cjk->codepointCount, 2U);
    EXPECT_EQ(cjk->lineCount, 1U);
    EXPECT_FLOAT_EQ(cjk->measuredSize.width, latin->measuredSize.width);
    EXPECT_FLOAT_EQ(cjk->measuredSize.height, latin->measuredSize.height);

    const auto multiLine = UI::measurePlaceholderText("A\nBC", {});
    ASSERT_TRUE(multiLine.has_value()) << (multiLine ? "" : multiLine.error().message);
    EXPECT_EQ(multiLine->codepointCount, 4U);
    EXPECT_EQ(multiLine->lineCount, 2U);
    EXPECT_FLOAT_EQ(multiLine->measuredSize.width, 16.0F * 0.6F * 2.0F);
    EXPECT_FLOAT_EQ(multiLine->measuredSize.height, 16.0F * 1.2F * 2.0F);
}

TEST(UITextTests, RejectsInvalidUtf8AndNonPositiveStyle)
{
    const auto invalidUtf8 =
        UI::measurePlaceholderText(std::string_view("\xC3\x28", 2), {});
    ASSERT_FALSE(invalidUtf8.has_value());
    EXPECT_EQ(invalidUtf8.error().code, UI::UIErrorCode::InvalidText);

    UI::UITextStyle style{};
    style.logicalSize = 0.0F;
    const auto invalidStyle = UI::measurePlaceholderText("A", style);
    ASSERT_FALSE(invalidStyle.has_value());
    EXPECT_EQ(invalidStyle.error().code, UI::UIErrorCode::InvalidText);
}

TEST(UITextTests, LabelAutoSizeUsesPlaceholderMetricsAndStoresUtf8)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    const Platform::WindowId window = *windowResult;

    auto context = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .textByteCapacity = 128,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle{};
    rootStyle.flex.alignItems = UI::UIAlignItems::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    auto labelResult = updater.createLabel(root.rootNodeId());
    ASSERT_TRUE(labelResult.has_value()) << (labelResult ? "" : labelResult.error().message);
    const UI::UINodeId label = *labelResult;

    assertOk(updater.setText(label, "你好"));

    auto textResult = updater.text(label);
    ASSERT_TRUE(textResult.has_value()) << (textResult ? "" : textResult.error().message);
    EXPECT_EQ(*textResult, "你好");

    auto styleResult = updater.textStyle(label);
    ASSERT_TRUE(styleResult.has_value()) << (styleResult ? "" : styleResult.error().message);
    EXPECT_FLOAT_EQ(styleResult->logicalSize, 16.0F);

    assertOk(context->commitLayout(UI::UILogicalSize{.width = 320.0F, .height = 180.0F}));
    const UI::UICommittedLayoutView layout = context->committedLayout();
    ASSERT_FALSE(layout.empty());

    const auto metrics = UI::measurePlaceholderText("你好", {});
    ASSERT_TRUE(metrics.has_value());
    bool foundLabel = false;
    for (const UI::UICommittedLayoutEntry& entry : layout.entries()) {
        if (entry.node != label) {
            continue;
        }
        foundLabel = true;
        EXPECT_FLOAT_EQ(entry.worldRect.width, metrics->measuredSize.width);
        EXPECT_FLOAT_EQ(entry.worldRect.height, metrics->measuredSize.height);
    }
    EXPECT_TRUE(foundLabel);

    const UI::UIContextStatistics stats = context->statistics();
    EXPECT_EQ(stats.textByteCapacity, 128U);
    EXPECT_GE(stats.textByteUsed, 6U);
    EXPECT_GE(stats.textByteHighWater, stats.textByteUsed);
}

TEST(UITextTests, RejectsPanelTextInvalidUtf8AndTextByteCapacity)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    const Platform::WindowId window = *windowResult;

    auto context = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .textByteCapacity = 4,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);

    auto panelResult = updater.createPanel(root.rootNodeId());
    ASSERT_TRUE(panelResult.has_value());
    const Core::Status panelText = updater.setText(*panelResult, "x");
    ASSERT_FALSE(panelText);
    EXPECT_EQ(panelText.error().code, UI::UIErrorCode::InvalidText);

    auto labelResult = updater.createLabel(root.rootNodeId());
    ASSERT_TRUE(labelResult.has_value());
    const Core::Status invalid =
        updater.setText(*labelResult, std::string_view("\xC3\x28", 2));
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, UI::UIErrorCode::InvalidText);

    const Core::Status overflow = updater.setText(*labelResult, "12345");
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);

    assertOk(updater.setText(*labelResult, "1234"));
    assertOk(updater.setText(*labelResult, ""));
    EXPECT_EQ(context->statistics().textByteUsed, 0U);
}

TEST(UITextTests, TextPlaceholderPaintEmitsPerCodepointSolidQuads)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    const Platform::WindowId window = *windowResult;

    auto context = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 8,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle{};
    rootStyle.flex.alignItems = UI::UIAlignItems::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    auto labelResult = updater.createLabel(root.rootNodeId());
    ASSERT_TRUE(labelResult.has_value());
    const UI::UINodeId label = *labelResult;
    UI::UITextStyle style{};
    style.color = {.red = 255, .green = 0, .blue = 0, .alpha = 128};
    assertOk(updater.setTextStyle(label, style));
    assertOk(updater.setText(label, "AB"));
    assertOk(context->commitLayout(UI::UILogicalSize{.width = 200.0F, .height = 100.0F}));

    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 2U);
    EXPECT_EQ(paint.entries()[0].node, label);
    EXPECT_EQ(paint.entries()[1].node, label);
    EXPECT_LT(paint.entries()[0].paintOrdinal, paint.entries()[1].paintOrdinal);
    // D10: placeholder raster packs monospaced cells into the glyph atlas.
    EXPECT_TRUE(paint.entries()[0].isGlyph);
    EXPECT_TRUE(paint.entries()[1].isGlyph);
    EXPECT_GT(paint.entries()[0].atlasWidth, 0U);
    EXPECT_GT(paint.entries()[0].atlasHeight, 0U);
    // Cursor advances by raster advance (9.6); draw width is atlas cell pixels.
    EXPECT_FLOAT_EQ(paint.entries()[0].worldRect.width, 9.0F);
    EXPECT_FLOAT_EQ(paint.entries()[1].worldRect.x, 9.6F);
    EXPECT_EQ(
        paint.entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{
            .red = 128,
            .green = 0,
            .blue = 0,
            .alpha = 128,
        }));
    EXPECT_FALSE(context->glyphAtlasPixels().empty());
    EXPECT_EQ(context->glyphAtlasWidth(), 512U);

    style.color.alpha = 0;
    assertOk(updater.setTextStyle(label, style));
    assertOk(context->commitLayout(UI::UILogicalSize{.width = 200.0F, .height = 100.0F}));
    EXPECT_TRUE(context->committedPaint().empty());
}

TEST(UITextTests, TextPaintUsesRasterizerAdvancesForMultiLine)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());

    auto context = createContext(
        *windowResult,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 16,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 16,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle{};
    rootStyle.flex.alignItems = UI::UIAlignItems::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    auto labelResult = updater.createLabel(root.rootNodeId());
    ASSERT_TRUE(labelResult.has_value());
    const UI::UINodeId label = *labelResult;
    UI::UITextStyle style{};
    style.color = {.red = 0, .green = 255, .blue = 0, .alpha = 255};
    assertOk(updater.setTextStyle(label, style));
    assertOk(updater.setText(label, "A\nBC"));
    assertOk(context->commitLayout(UI::UILogicalSize{.width = 200.0F, .height = 100.0F}));

    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 3U);
    EXPECT_EQ(paint.entries()[0].node, label);
    EXPECT_EQ(paint.entries()[1].node, label);
    EXPECT_EQ(paint.entries()[2].node, label);
    // "A\nBC": A on line 0; B then C on line 1. Placeholder drawY tracks cursorY.
    EXPECT_FLOAT_EQ(
        paint.entries()[1].worldRect.y,
        paint.entries()[0].worldRect.y + 16.0F * 1.2F);
    EXPECT_FLOAT_EQ(paint.entries()[1].worldRect.x, paint.entries()[0].worldRect.x);
    EXPECT_FLOAT_EQ(paint.entries()[2].worldRect.x, 9.6F);
    EXPECT_FLOAT_EQ(paint.entries()[2].worldRect.y, paint.entries()[1].worldRect.y);
    EXPECT_TRUE(paint.entries()[0].isGlyph);
    EXPECT_TRUE(paint.entries()[1].isGlyph);
    EXPECT_TRUE(paint.entries()[2].isGlyph);
}

TEST(UITextTests, SameTextIsNoOpAndClearingTextShrinksAutoSize)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    const Platform::WindowId window = *windowResult;

    auto context = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle{};
    rootStyle.flex.alignItems = UI::UIAlignItems::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));
    auto labelResult = updater.createLabel(root.rootNodeId());
    ASSERT_TRUE(labelResult.has_value());
    const UI::UINodeId label = *labelResult;

    assertOk(updater.setText(label, "Hello"));
    assertOk(context->commitLayout(UI::UILogicalSize{.width = 200.0F, .height = 100.0F}));
    const u64 layoutRevision = context->statistics().layoutRevision;
    const u64 paintRevision = context->statistics().paintRevision;

    assertOk(updater.setText(label, "Hello"));
    EXPECT_EQ(context->statistics().layoutRevision, layoutRevision);
    EXPECT_EQ(context->statistics().paintRevision, paintRevision);

    assertOk(updater.setText(label, ""));
    assertOk(context->commitLayout(UI::UILogicalSize{.width = 200.0F, .height = 100.0F}));
    bool found = false;
    for (const UI::UICommittedLayoutEntry& entry : context->committedLayout().entries()) {
        if (entry.node != label) {
            continue;
        }
        found = true;
        EXPECT_FLOAT_EQ(entry.worldRect.width, 0.0F);
        EXPECT_FLOAT_EQ(entry.worldRect.height, 0.0F);
    }
    EXPECT_TRUE(found);
}

TEST(UITextTests, ImeFocusCompositionAndCommitAppendToLabel)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    const Platform::WindowId window = *windowResult;

    auto context = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .textByteCapacity = 256,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    auto updater = createUpdater(*context, root);
    UI::UILayoutStyle rootStyle{};
    rootStyle.flex.alignItems = UI::UIAlignItems::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    auto labelResult = updater.createLabel(root.rootNodeId());
    ASSERT_TRUE(labelResult.has_value());
    const UI::UINodeId label = *labelResult;
    UI::UILayoutStyle labelStyle{};
    labelStyle.size.width = UI::UILayoutLength::Px(100.0F);
    labelStyle.size.height = UI::UILayoutLength::Px(40.0F);
    assertOk(updater.setLayoutStyle(label, labelStyle));
    assertOk(updater.setText(label, "Hi"));
    assertOk(context->commitLayout(UI::UILogicalSize{.width = 200.0F, .height = 100.0F}));

    EXPECT_FALSE(context->imeFocus().hasValue());

    // Primary Down on the Label sets IME focus.
    auto down = context->routePointerInput(UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{1},
        .transitionOrdinal = 0,
        .sourceSequence = 1,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = UI::UIRoutedPointerEventKind::ButtonDown,
        .position = {.x = 10.0F, .y = 10.0F},
        .button = Platform::PointerButton::Primary,
    });
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_EQ(context->imeFocus(), label);

    auto composition = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{2},
        2,
        "ni",
        2,
        Platform::TextCompositionStage::Started);
    ASSERT_TRUE(composition.has_value()) << (composition ? "" : composition.error().message);
    EXPECT_TRUE(composition->consumed);
    EXPECT_TRUE(composition->applied);
    EXPECT_TRUE(context->imeCompositionActive());
    EXPECT_EQ(context->imePreeditUtf8(), "ni");
    EXPECT_EQ(context->imePreeditCursorCodepoint(), 2U);

    auto updated = context->routeTextComposition(
        window,
        Platform::PlatformFrameId{3},
        3,
        "你",
        1,
        Platform::TextCompositionStage::Updated);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(context->imePreeditUtf8(), "你");

    auto commit = context->routeTextInput(
        window,
        Platform::PlatformFrameId{4},
        4,
        "好");
    ASSERT_TRUE(commit.has_value()) << (commit ? "" : commit.error().message);
    EXPECT_TRUE(commit->consumed);
    EXPECT_TRUE(commit->applied);
    EXPECT_FALSE(context->imeCompositionActive());
    EXPECT_TRUE(context->imePreeditUtf8().empty());

    auto text = updater.text(label);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Hi好");
}

TEST(UITextTests, TextInputWithoutImeFocusIsNotConsumed)
{
    auto windowsResult = WindowPool::Create(1);
    ASSERT_TRUE(windowsResult.has_value());
    WindowPool windows = std::move(*windowsResult);
    auto windowResult = windows.tryEmplace(1);
    ASSERT_TRUE(windowResult.has_value());
    const Platform::WindowId window = *windowResult;

    auto context = createContext(window, {.nodeCapacity = 4, .rootCapacity = 1});
    ASSERT_NE(context, nullptr);

    auto idle = context->routeTextInput(
        window,
        Platform::PlatformFrameId{1},
        1,
        "x");
    ASSERT_TRUE(idle.has_value());
    EXPECT_FALSE(idle->consumed);
    EXPECT_FALSE(idle->applied);
}

} // namespace Tina::Tests
