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

} // namespace Tina::Tests
