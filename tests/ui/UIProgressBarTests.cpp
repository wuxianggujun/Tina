#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <limits>
#include <memory>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 8,
        .routePathCapacity = 8,
    })
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

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style{};
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UIBoxPaint solidFill(
    u8 red,
    u8 green,
    u8 blue,
    u8 alpha = 255) noexcept
{
    UI::UIBoxPaint paint{};
    paint.solidFill = UI::UISolidFill{
        .color = {
            .red = red,
            .green = green,
            .blue = blue,
            .alpha = alpha,
        },
    };
    return paint;
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

void expectInvalidControlValue(Core::Status status)
{
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidControlValue);
}

[[nodiscard]] const UI::UICommittedNodeEntry* findStructureEntry(
    UI::UICommittedStructureView view,
    UI::UINodeId node) noexcept
{
    for (const UI::UICommittedNodeEntry& entry : view.entries()) {
        if (entry.node == node) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const UI::UICommittedHitEntry* findHitEntry(
    UI::UICommittedHitView view,
    UI::UINodeId node) noexcept
{
    for (const UI::UICommittedHitEntry& entry : view.entries()) {
        if (entry.node == node) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const UI::UISemanticsEntry* findSemanticsEntry(
    UI::UICommittedSemanticsView view,
    UI::UINodeId node) noexcept
{
    for (const UI::UISemanticsEntry& entry : view.entries()) {
        if (entry.node == node) {
            return &entry;
        }
    }
    return nullptr;
}

class UIProgressBarTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;

        context = createContext(window);
        ASSERT_NE(context, nullptr);
        root = createRoot(*context);
        ASSERT_TRUE(root.hasValue());
        updater = createUpdater(*context, root);
        assertOk(updater.setLayoutStyle(
            root.rootNodeId(),
            fixedSize(200.0F, 60.0F)));
    }

    [[nodiscard]] UI::UINodeId createProgressBar(
        float width = 120.0F,
        float height = 20.0F)
    {
        auto result = updater.createProgressBar(root.rootNodeId());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        assertOk(updater.setLayoutStyle(*result, fixedSize(width, height)));
        return *result;
    }

    void publishLayout()
    {
        assertOk(context->commitLayout({.width = 200.0F, .height = 60.0F}));
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
};

TEST_F(UIProgressBarTest, DefaultsToZeroAndIgnoredPointerPolicy)
{
    const UI::UINodeId progressBar = createProgressBar();
    ASSERT_TRUE(progressBar.hasValue());

    auto value = updater.progressBarValue(progressBar);
    ASSERT_TRUE(value.has_value()) << (value ? "" : value.error().message);
    EXPECT_FLOAT_EQ(*value, 0.0F);
    auto paint = updater.progressBarPaint(progressBar);
    ASSERT_TRUE(paint.has_value()) << (paint ? "" : paint.error().message);
    EXPECT_EQ(*paint, UI::UIProgressBarPaint{});

    publishLayout();
    const UI::UICommittedNodeEntry* const structureEntry =
        findStructureEntry(context->committedStructure(), progressBar);
    ASSERT_NE(structureEntry, nullptr);
    EXPECT_EQ(structureEntry->kind, UI::UIWidgetKind::ProgressBar);

    const UI::UICommittedHitEntry* const hitEntry =
        findHitEntry(context->committedHit(), progressBar);
    ASSERT_NE(hitEntry, nullptr);
    EXPECT_EQ(hitEntry->policy, UI::UIPointerHitPolicy::Ignore);
    EXPECT_FALSE(context->queryPointerHit({10.0F, 10.0F}).hasTarget());
}

TEST_F(UIProgressBarTest, RangeAndValueClampAndPublishSemantics)
{
    const UI::UINodeId progressBar = createProgressBar();
    ASSERT_TRUE(progressBar.hasValue());

    assertOk(updater.setProgressBarRange(progressBar, 10.0F, 30.0F));
    auto value = updater.progressBarValue(progressBar);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 10.0F);

    assertOk(updater.setProgressBarValue(progressBar, 100.0F));
    value = updater.progressBarValue(progressBar);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 30.0F);

    assertOk(updater.setProgressBarValue(progressBar, 15.0F));
    const UI::UIProgressBarPaint expectedPaint{
        .fillColor = {.red = 200, .green = 100, .blue = 50, .alpha = 128},
    };
    assertOk(updater.setProgressBarPaint(progressBar, expectedPaint));
    auto paint = updater.progressBarPaint(progressBar);
    ASSERT_TRUE(paint.has_value()) << (paint ? "" : paint.error().message);
    EXPECT_EQ(*paint, expectedPaint);

    publishLayout();
    const UI::UISemanticsEntry* const semantics =
        findSemanticsEntry(context->committedSemantics(), progressBar);
    ASSERT_NE(semantics, nullptr);
    EXPECT_EQ(semantics->role, UI::UISemanticsRole::ProgressBar);
    EXPECT_EQ(semantics->kind, UI::UIWidgetKind::ProgressBar);
    EXPECT_TRUE(semantics->hasRange);
    EXPECT_FLOAT_EQ(semantics->minValue, 10.0F);
    EXPECT_FLOAT_EQ(semantics->maxValue, 30.0F);
    EXPECT_FLOAT_EQ(semantics->value, 15.0F);
    EXPECT_TRUE(semantics->enabled);
    EXPECT_FALSE(semantics->focused);
}

TEST_F(UIProgressBarTest, TrackAndFillPublishDeterministicGeometry)
{
    const UI::UINodeId progressBar = createProgressBar(120.0F, 20.0F);
    ASSERT_TRUE(progressBar.hasValue());
    assertOk(updater.setBoxPaint(progressBar, solidFill(20, 40, 60, 128)));
    assertOk(updater.setProgressBarRange(progressBar, 10.0F, 30.0F));
    assertOk(updater.setProgressBarValue(progressBar, 15.0F));
    assertOk(updater.setProgressBarPaint(
        progressBar,
        {.fillColor = {.red = 200, .green = 100, .blue = 50, .alpha = 128}}));

    publishLayout();
    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 2U);
    const UI::UICommittedPaintEntry& track = paint.entries()[0];
    const UI::UICommittedPaintEntry& fill = paint.entries()[1];
    EXPECT_EQ(track.node, progressBar);
    EXPECT_EQ(fill.node, progressBar);
    EXPECT_EQ(track.paintOrdinal, 1U);
    EXPECT_EQ(fill.paintOrdinal, 2U);
    EXPECT_EQ(
        track.worldRect,
        (UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 120.0F, .height = 20.0F}));
    EXPECT_EQ(
        fill.worldRect,
        (UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 30.0F, .height = 20.0F}));
    EXPECT_EQ(track.effectiveClip, fill.effectiveClip);
    EXPECT_EQ(
        track.solidFill,
        (UI::UIPremultipliedRgba8Color{
            .red = 10,
            .green = 20,
            .blue = 30,
            .alpha = 128,
        }));
    EXPECT_EQ(
        fill.solidFill,
        (UI::UIPremultipliedRgba8Color{
            .red = 100,
            .green = 50,
            .blue = 25,
            .alpha = 128,
        }));
}

TEST_F(UIProgressBarTest, PaintCapacityCountsValueDerivedFillAtomically)
{
    auto limitedContext = createContext(
        window,
        {
            .nodeCapacity = 2,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 1,
        });
    ASSERT_NE(limitedContext, nullptr);
    auto limitedRoot = createRoot(*limitedContext);
    ASSERT_TRUE(limitedRoot.hasValue());
    auto limitedUpdater = createUpdater(*limitedContext, limitedRoot);
    auto progressResult = limitedUpdater.createProgressBar(limitedRoot.rootNodeId());
    ASSERT_TRUE(progressResult.has_value())
        << (progressResult ? "" : progressResult.error().message);
    const UI::UINodeId progressBar = *progressResult;
    assertOk(limitedUpdater.setLayoutStyle(
        limitedRoot.rootNodeId(),
        fixedSize(100.0F, 20.0F)));
    assertOk(limitedUpdater.setLayoutStyle(progressBar, fixedSize(100.0F, 20.0F)));
    assertOk(limitedUpdater.setBoxPaint(progressBar, solidFill(1, 2, 3)));
    assertOk(limitedUpdater.setProgressBarPaint(
        progressBar,
        {.fillColor = {.red = 4, .green = 5, .blue = 6, .alpha = 255}}));
    assertOk(limitedContext->commitLayout({.width = 100.0F, .height = 20.0F}));
    ASSERT_EQ(limitedContext->committedPaint().size(), 1U);
    const u64 publishedRevision = limitedContext->committedPaint().paintRevision();

    assertOk(limitedUpdater.setProgressBarValue(progressBar, 0.5F));
    const Core::Status overflow =
        limitedContext->commitLayout({.width = 100.0F, .height = 20.0F});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(limitedContext->committedPaint().paintRevision(), publishedRevision);
    ASSERT_EQ(limitedContext->committedPaint().size(), 1U);
    EXPECT_EQ(limitedContext->committedPaint().entries()[0].node, progressBar);
    EXPECT_TRUE(limitedContext->statistics().paintDirty);

    assertOk(limitedUpdater.setProgressBarValue(progressBar, 0.0F));
    assertOk(limitedContext->commitLayout({.width = 100.0F, .height = 20.0F}));
    EXPECT_EQ(limitedContext->committedPaint().size(), 1U);
}

TEST_F(UIProgressBarTest, RejectsInvalidInputsAndWrongKindsWithoutMutation)
{
    const UI::UINodeId progressBar = createProgressBar();
    ASSERT_TRUE(progressBar.hasValue());
    auto buttonResult = updater.createButton(root.rootNodeId());
    ASSERT_TRUE(buttonResult.has_value()) << (buttonResult ? "" : buttonResult.error().message);
    const UI::UINodeId button = *buttonResult;

    assertOk(updater.setProgressBarRange(progressBar, 5.0F, 10.0F));
    assertOk(updater.setProgressBarValue(progressBar, 7.0F));
    const UI::UIProgressBarPaint expectedPaint{
        .fillColor = {.red = 9, .green = 8, .blue = 7, .alpha = 6},
    };
    assertOk(updater.setProgressBarPaint(progressBar, expectedPaint));

    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    const float infinity = (std::numeric_limits<float>::infinity)();
    expectInvalidControlValue(updater.setProgressBarRange(progressBar, nan, 10.0F));
    expectInvalidControlValue(updater.setProgressBarRange(progressBar, 5.0F, infinity));
    expectInvalidControlValue(updater.setProgressBarRange(progressBar, 5.0F, 5.0F));
    expectInvalidControlValue(updater.setProgressBarRange(progressBar, 10.0F, 5.0F));
    expectInvalidControlValue(updater.setProgressBarValue(progressBar, nan));
    expectInvalidControlValue(updater.setProgressBarValue(progressBar, infinity));

    expectInvalidControlValue(updater.setProgressBarRange(button, 0.0F, 1.0F));
    expectInvalidControlValue(updater.setProgressBarValue(button, 0.5F));
    expectInvalidControlValue(updater.setProgressBarPaint(button, expectedPaint));
    auto wrongValue = updater.progressBarValue(button);
    ASSERT_FALSE(wrongValue.has_value());
    EXPECT_EQ(wrongValue.error().code, UI::UIErrorCode::InvalidControlValue);
    auto wrongPaint = updater.progressBarPaint(button);
    ASSERT_FALSE(wrongPaint.has_value());
    EXPECT_EQ(wrongPaint.error().code, UI::UIErrorCode::InvalidControlValue);

    auto value = updater.progressBarValue(progressBar);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 7.0F);
    auto paint = updater.progressBarPaint(progressBar);
    ASSERT_TRUE(paint.has_value());
    EXPECT_EQ(*paint, expectedPaint);

    publishLayout();
    const UI::UISemanticsEntry* const semantics =
        findSemanticsEntry(context->committedSemantics(), progressBar);
    ASSERT_NE(semantics, nullptr);
    EXPECT_FLOAT_EQ(semantics->minValue, 5.0F);
    EXPECT_FLOAT_EQ(semantics->maxValue, 10.0F);
    EXPECT_FLOAT_EQ(semantics->value, 7.0F);
}

} // namespace
} // namespace Tina::Tests
