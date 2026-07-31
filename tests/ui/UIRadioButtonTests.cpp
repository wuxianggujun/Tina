#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <array>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 12,
        .rootCapacity = 1,
        .paintSnapshotCapacity = 12,
        .routePathCapacity = 12,
        .buttonActionCapacity = 8,
        .textByteCapacity = 256,
    })
{
    capacities.applyDefaultProductChrome = false;
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

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

void expectInvalidControlValue(Core::Status status)
{
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidControlValue);
}

[[nodiscard]] bool isSelected(
    UI::UITreeUpdater& updater,
    UI::UINodeId radioButton)
{
    auto result = updater.isRadioButtonSelected(radioButton);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : false;
}

[[nodiscard]] bool isPressed(
    UI::UITreeUpdater& updater,
    UI::UINodeId radioButton)
{
    auto result = updater.isRadioButtonPressed(radioButton);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : false;
}

[[nodiscard]] UI::UIPointerInputEvent makePointerInput(
    Platform::WindowId window,
    UI::UIRoutedPointerEventKind kind,
    u64 sequence,
    UI::UILogicalPoint position) noexcept
{
    return UI::UIPointerInputEvent{
        .platformFrame = Platform::PlatformFrameId{sequence},
        .transitionOrdinal = static_cast<usize>(sequence - 1),
        .sourceSequence = sequence,
        .window = window,
        .pointer = Platform::PrimaryPointerId,
        .kind = kind,
        .position = position,
        .delta = kind == UI::UIRoutedPointerEventKind::Move
            ? UI::UILogicalPoint{.x = 1.0F, .y = 1.0F}
            : UI::UILogicalPoint{},
        .button = Platform::PointerButton::Primary,
    };
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

[[nodiscard]] const UI::UICommittedLayoutEntry* findLayoutEntry(
    UI::UICommittedLayoutView view,
    UI::UINodeId node) noexcept
{
    for (const UI::UICommittedLayoutEntry& entry : view.entries()) {
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

struct ActivationLog final {
    void record(const UI::UIButtonActionEvent& event) noexcept
    {
        ++count;
        node = event.buttonNode;
        source = event.source;
        platformFrame = event.platformFrame;
        sourceSequence = event.sourceSequence;
    }

    int count = 0;
    UI::UINodeId node{};
    UI::UIButtonActivationSource source =
        UI::UIButtonActivationSource::PrimaryPointer;
    Platform::PlatformFrameId platformFrame{};
    u64 sourceSequence = 0;
};

[[nodiscard]] UI::UIButtonActionCallback actionFor(ActivationLog& log) noexcept
{
    return UI::UIButtonActionCallback{
        [&log](const UI::UIButtonActionEvent& event) noexcept {
            log.record(event);
        }};
}

class UIRadioButtonTest : public testing::Test {
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
            fixedSize(160.0F, 120.0F)));
    }

    [[nodiscard]] UI::UINodeId createRadioButton(
        UI::UINodeId parent,
        float width = 80.0F,
        float height = 24.0F)
    {
        auto result = updater.createElement(parent, UI::makeRadioButtonElement());
        EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
        if (!result) {
            return {};
        }
        assertOk(updater.setLayoutStyle(*result, fixedSize(width, height)));
        return *result;
    }

    [[nodiscard]] UI::UINodeId createRadioButton(
        float width = 80.0F,
        float height = 24.0F)
    {
        return createRadioButton(root.rootNodeId(), width, height);
    }

    void publishLayout()
    {
        assertOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
};

TEST_F(UIRadioButtonTest, DefaultsExposeTextPaintCapabilitiesAndSemantics)
{
    const UI::UINodeId radioButton = createRadioButton();
    ASSERT_TRUE(radioButton.hasValue());
    EXPECT_FALSE(isSelected(updater, radioButton));
    EXPECT_FALSE(isPressed(updater, radioButton));

    auto defaultPaint = updater.radioButtonPaint(radioButton);
    ASSERT_TRUE(defaultPaint.has_value())
        << (defaultPaint ? "" : defaultPaint.error().message);
    EXPECT_EQ(*defaultPaint, UI::UIRadioButtonPaint{});

    const UI::UIRadioButtonPaint expectedPaint{
        .indicatorColor = {.red = 20, .green = 30, .blue = 40, .alpha = 255},
        .selectedIndicatorColor = {.red = 200, .green = 210, .blue = 220, .alpha = 255},
        .selectedIndicatorInset = 4.0F,
        .labelGap = 10.0F,
    };
    assertOk(updater.setRadioButtonPaint(radioButton, expectedPaint));
    auto paint = updater.radioButtonPaint(radioButton);
    ASSERT_TRUE(paint.has_value()) << (paint ? "" : paint.error().message);
    EXPECT_EQ(*paint, expectedPaint);

    assertOk(updater.setText(radioButton, "Graphics"));
    auto text = updater.text(radioButton);
    ASSERT_TRUE(text.has_value()) << (text ? "" : text.error().message);
    EXPECT_EQ(*text, "Graphics");
    assertOk(updater.setRadioButtonSelected(radioButton, true));
    publishLayout();

    const UI::UICommittedHitEntry* const hitEntry =
        findHitEntry(context->committedHit(), radioButton);
    ASSERT_NE(hitEntry, nullptr);
    EXPECT_EQ(hitEntry->policy, UI::UIPointerHitPolicy::Targetable);
    EXPECT_TRUE(UI::hasBehavior(hitEntry->behaviors, UI::UIElementBehavior::Focusable));
    EXPECT_TRUE(UI::hasBehavior(hitEntry->behaviors, UI::UIElementBehavior::Activate));
    EXPECT_TRUE(UI::hasBehavior(hitEntry->behaviors, UI::UIElementBehavior::ExclusiveChoice));

    const UI::UISemanticsEntry* const semantics =
        findSemanticsEntry(context->committedSemantics(), radioButton);
    ASSERT_NE(semantics, nullptr);
    EXPECT_EQ(semantics->role, UI::UISemanticsRole::RadioButton);
    EXPECT_TRUE(UI::hasSemanticsAction(semantics->actions, UI::UISemanticsAction::Toggle));
    EXPECT_EQ(semantics->name, "Graphics");
    EXPECT_TRUE(semantics->checked);
    EXPECT_TRUE(semantics->enabled);
    EXPECT_FALSE(semantics->focused);
}

TEST_F(UIRadioButtonTest, DirectParentScopesExclusiveSelectionAndSetterIsSilent)
{
    auto firstGroupResult = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    auto secondGroupResult = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(firstGroupResult.has_value())
        << (firstGroupResult ? "" : firstGroupResult.error().message);
    ASSERT_TRUE(secondGroupResult.has_value())
        << (secondGroupResult ? "" : secondGroupResult.error().message);
    const UI::UINodeId first = createRadioButton(*firstGroupResult);
    const UI::UINodeId second = createRadioButton(*firstGroupResult);
    const UI::UINodeId independent = createRadioButton(*secondGroupResult);
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    ASSERT_TRUE(independent.hasValue());

    ActivationLog firstLog{};
    ActivationLog secondLog{};
    ActivationLog independentLog{};
    assertOk(updater.setRadioButtonAction(first, actionFor(firstLog)));
    assertOk(updater.setRadioButtonAction(second, actionFor(secondLog)));
    assertOk(updater.setRadioButtonAction(independent, actionFor(independentLog)));

    assertOk(updater.setRadioButtonSelected(first, true));
    EXPECT_TRUE(isSelected(updater, first));
    EXPECT_FALSE(isSelected(updater, second));
    EXPECT_FALSE(isSelected(updater, independent));

    assertOk(updater.setRadioButtonSelected(second, true));
    EXPECT_FALSE(isSelected(updater, first));
    EXPECT_TRUE(isSelected(updater, second));
    EXPECT_FALSE(isSelected(updater, independent));

    assertOk(updater.setRadioButtonSelected(independent, true));
    EXPECT_FALSE(isSelected(updater, first));
    EXPECT_TRUE(isSelected(updater, second));
    EXPECT_TRUE(isSelected(updater, independent));

    assertOk(updater.setRadioButtonSelected(second, false));
    EXPECT_FALSE(isSelected(updater, first));
    EXPECT_FALSE(isSelected(updater, second));
    EXPECT_TRUE(isSelected(updater, independent));
    EXPECT_EQ(firstLog.count, 0);
    EXPECT_EQ(secondLog.count, 0);
    EXPECT_EQ(independentLog.count, 0);
}

TEST_F(UIRadioButtonTest, IndicatorPaintPublishesDeterministicGeometry)
{
    const UI::UINodeId radioButton = createRadioButton(80.0F, 24.0F);
    ASSERT_TRUE(radioButton.hasValue());
    assertOk(updater.setRadioButtonPaint(
        radioButton,
        {
            .indicatorColor = {.red = 20, .green = 40, .blue = 60, .alpha = 128},
            .selectedIndicatorColor = {.red = 200, .green = 100, .blue = 50, .alpha = 255},
            .selectedIndicatorInset = 4.0F,
            .labelGap = 8.0F,
        }));
    assertOk(updater.setRadioButtonSelected(radioButton, true));

    publishLayout();
    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 2U);
    const UI::UICommittedPaintEntry& track = paint.entries()[0];
    const UI::UICommittedPaintEntry& indicator = paint.entries()[1];
    EXPECT_EQ(track.node, radioButton);
    EXPECT_EQ(indicator.node, radioButton);
    EXPECT_EQ(track.paintOrdinal, 1U);
    EXPECT_EQ(indicator.paintOrdinal, 2U);
    EXPECT_EQ(
        track.worldRect,
        (UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 24.0F, .height = 24.0F}));
    EXPECT_EQ(
        indicator.worldRect,
        (UI::UILogicalRect{.x = 4.0F, .y = 4.0F, .width = 16.0F, .height = 16.0F}));
    EXPECT_EQ(track.effectiveClip, indicator.effectiveClip);
    EXPECT_EQ(
        track.solidFill,
        (UI::UIPremultipliedRgba8Color{
            .red = 10,
            .green = 20,
            .blue = 30,
            .alpha = 128,
        }));
    EXPECT_EQ(
        indicator.solidFill,
        (UI::UIPremultipliedRgba8Color{
            .red = 200,
            .green = 100,
            .blue = 50,
            .alpha = 255,
        }));
}

TEST_F(UIRadioButtonTest, HoveredFocusedPressedAndDisabledStatesResolveCommittedIndicatorPaint)
{
    const UI::UINodeId radioButton = createRadioButton(40.0F, 24.0F);
    ASSERT_TRUE(radioButton.hasValue());
    assertOk(updater.setRadioButtonPaint(
        radioButton,
        {
            .indicatorColor = {.red = 10, .green = 20, .blue = 30, .alpha = 255},
            .selectedIndicatorColor = {},
            .selectedIndicatorInset = 4.0F,
            .labelGap = 8.0F,
            .hoveredIndicatorColor = {.red = 100, .green = 110, .blue = 120, .alpha = 255},
            .focusedIndicatorColor = {.red = 40, .green = 50, .blue = 60, .alpha = 255},
            .pressedIndicatorColor = {.red = 70, .green = 80, .blue = 90, .alpha = 255},
        }));

    publishLayout();
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{10, 20, 30, 255}));

    assertOk(context->requestFocus(radioButton));
    publishLayout();
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{40, 50, 60, 255}));

    auto movedInside = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::Move,
        1,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(movedInside.has_value()) << (movedInside ? "" : movedInside.error().message);
    publishLayout();
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{100, 110, 120, 255}));

    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        2,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(isPressed(updater, radioButton));
    EXPECT_EQ(context->defaultActionFocus(), radioButton);
    publishLayout();
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{70, 80, 90, 255}));

    auto up = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        3,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(isPressed(updater, radioButton));
    EXPECT_EQ(context->defaultActionFocus(), radioButton);
    publishLayout();
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{100, 110, 120, 255}));

    auto movedOutside = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::Move,
        4,
        {.x = 80.0F, .y = 80.0F}));
    ASSERT_TRUE(movedOutside.has_value()) << (movedOutside ? "" : movedOutside.error().message);
    publishLayout();
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{40, 50, 60, 255}));

    assertOk(updater.setEnabled(radioButton, false));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    publishLayout();
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{5, 11, 16, 140}));
}

TEST_F(UIRadioButtonTest, AutoWidthUsesResolvedHeightAndTracksLabelGap)
{
    UI::UILayoutStyle rootStyle = fixedSize(160.0F, 120.0F);
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    auto radioResult = updater.createElement(root.rootNodeId(), UI::makeRadioButtonElement());
    ASSERT_TRUE(radioResult.has_value()) << radioResult.error().message;
    const UI::UINodeId radioButton = *radioResult;
    UI::UILayoutStyle style{};
    style.size.height = UI::UILayoutLength::Px(40.0F);
    assertOk(updater.setLayoutStyle(radioButton, style));
    assertOk(updater.setText(radioButton, "AB"));
    assertOk(updater.setRadioButtonPaint(
        radioButton,
        {.labelGap = 10.0F}));
    publishLayout();

    const UI::UICommittedLayoutEntry* firstLayout =
        findLayoutEntry(context->committedLayout(), radioButton);
    ASSERT_NE(firstLayout, nullptr);
    const float firstWidth = firstLayout->worldRect.width;
    EXPECT_GT(firstWidth, 50.0F);
    EXPECT_FLOAT_EQ(firstLayout->worldRect.height, 40.0F);

    assertOk(updater.setRadioButtonPaint(
        radioButton,
        {.labelGap = 20.0F}));
    EXPECT_TRUE(context->statistics().layoutDirty);
    publishLayout();
    const UI::UICommittedLayoutEntry* secondLayout =
        findLayoutEntry(context->committedLayout(), radioButton);
    ASSERT_NE(secondLayout, nullptr);
    EXPECT_FLOAT_EQ(secondLayout->worldRect.width, firstWidth + 10.0F);
    EXPECT_FLOAT_EQ(secondLayout->worldRect.height, 40.0F);
}

TEST_F(UIRadioButtonTest, EmptyLabelAutoSizeKeepsIndicatorVisible)
{
    UI::UILayoutStyle rootStyle = fixedSize(160.0F, 120.0F);
    rootStyle.flexContainer.alignItems = UI::UIAxisAlignment::Start;
    assertOk(updater.setLayoutStyle(root.rootNodeId(), rootStyle));

    auto radioResult = updater.createElement(root.rootNodeId(), UI::makeRadioButtonElement());
    ASSERT_TRUE(radioResult.has_value()) << (radioResult ? "" : radioResult.error().message);
    const UI::UINodeId radioButton = *radioResult;
    assertOk(updater.setRadioButtonPaint(
        radioButton,
        {.indicatorColor = {.red = 255, .green = 255, .blue = 255, .alpha = 255}}));
    publishLayout();

    const UI::UICommittedLayoutEntry* layout =
        findLayoutEntry(context->committedLayout(), radioButton);
    ASSERT_NE(layout, nullptr);
    EXPECT_GT(layout->worldRect.width, 0.0F);
    EXPECT_GT(layout->worldRect.height, 0.0F);
    EXPECT_FLOAT_EQ(layout->worldRect.width, layout->worldRect.height);

    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 1U);
    EXPECT_EQ(paint.entries()[0].node, radioButton);
    EXPECT_GT(paint.entries()[0].worldRect.width, 0.0F);
    EXPECT_GT(paint.entries()[0].worldRect.height, 0.0F);
}

TEST_F(UIRadioButtonTest, GroupDirtyCapacityFailurePreservesSelectionAtomically)
{
    auto limitedContext = createContext(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 3,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
            .paintSnapshotCapacity = 3,
            .buttonActionCapacity = 2,
            .textByteCapacity = 64,
        });
    ASSERT_NE(limitedContext, nullptr);
    auto limitedRoot = createRoot(*limitedContext);
    ASSERT_TRUE(limitedRoot.hasValue());
    auto limitedUpdater = createUpdater(*limitedContext, limitedRoot);
    auto firstResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makeRadioButtonElement());
    auto secondResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makeRadioButtonElement());
    ASSERT_TRUE(firstResult.has_value());
    ASSERT_TRUE(secondResult.has_value());
    const UI::UINodeId first = *firstResult;
    const UI::UINodeId second = *secondResult;
    assertOk(limitedContext->commitLayout({.width = 160.0F, .height = 120.0F}));

    assertOk(limitedUpdater.setRadioButtonSelected(first, true));
    assertOk(limitedContext->commitLayout({.width = 160.0F, .height = 120.0F}));
    const Core::Status rejected = limitedUpdater.setRadioButtonSelected(second, true);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_TRUE(isSelected(limitedUpdater, first));
    EXPECT_FALSE(isSelected(limitedUpdater, second));
    EXPECT_FALSE(limitedContext->statistics().paintDirty);
    EXPECT_FALSE(limitedContext->statistics().semanticsDirty);
}

TEST_F(UIRadioButtonTest, PaintCapacityCountsSelectedIndicatorAtomically)
{
    auto limitedContext = createContext(
        window,
        {
            .nodeCapacity = 2,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 1,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(limitedContext, nullptr);
    auto limitedRoot = createRoot(*limitedContext);
    ASSERT_TRUE(limitedRoot.hasValue());
    auto limitedUpdater = createUpdater(*limitedContext, limitedRoot);
    auto radioResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makeRadioButtonElement());
    ASSERT_TRUE(radioResult.has_value()) << (radioResult ? "" : radioResult.error().message);
    const UI::UINodeId radioButton = *radioResult;
    assertOk(limitedUpdater.setLayoutStyle(
        limitedRoot.rootNodeId(),
        fixedSize(40.0F, 24.0F)));
    assertOk(limitedUpdater.setLayoutStyle(radioButton, fixedSize(40.0F, 24.0F)));
    assertOk(limitedUpdater.setRadioButtonPaint(
        radioButton,
        {
            .indicatorColor = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
            .selectedIndicatorColor = {.red = 4, .green = 5, .blue = 6, .alpha = 255},
            .selectedIndicatorInset = 4.0F,
            .labelGap = 8.0F,
        }));
    assertOk(limitedContext->commitLayout({.width = 40.0F, .height = 24.0F}));
    ASSERT_EQ(limitedContext->committedPaint().size(), 1U);
    const u64 publishedRevision = limitedContext->committedPaint().paintRevision();

    assertOk(limitedUpdater.setRadioButtonSelected(radioButton, true));
    const Core::Status overflow =
        limitedContext->commitLayout({.width = 40.0F, .height = 24.0F});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(limitedContext->committedPaint().paintRevision(), publishedRevision);
    ASSERT_EQ(limitedContext->committedPaint().size(), 1U);
    EXPECT_EQ(limitedContext->committedPaint().entries()[0].node, radioButton);
    EXPECT_TRUE(limitedContext->statistics().paintDirty);

    assertOk(limitedUpdater.setRadioButtonSelected(radioButton, false));
    assertOk(limitedContext->commitLayout({.width = 40.0F, .height = 24.0F}));
    EXPECT_EQ(limitedContext->committedPaint().size(), 1U);
}

TEST_F(UIRadioButtonTest, FocusedOverrideIsIncludedInPaintCapacityPreflight)
{
    auto limitedContext = createContext(
        window,
        {
            .nodeCapacity = 2,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 1,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(limitedContext, nullptr);
    auto limitedRoot = createRoot(*limitedContext);
    ASSERT_TRUE(limitedRoot.hasValue());
    auto limitedUpdater = createUpdater(*limitedContext, limitedRoot);
    auto radioResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makeRadioButtonElement());
    ASSERT_TRUE(radioResult.has_value()) << (radioResult ? "" : radioResult.error().message);
    const UI::UINodeId radioButton = *radioResult;
    assertOk(limitedUpdater.setLayoutStyle(
        limitedRoot.rootNodeId(),
        fixedSize(40.0F, 24.0F)));
    assertOk(limitedUpdater.setLayoutStyle(radioButton, fixedSize(40.0F, 24.0F)));

    UI::UIBoxPaint rootPaint{};
    rootPaint.solidFill = UI::UISolidFill{
        .color = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
    };
    assertOk(limitedUpdater.setBoxPaint(limitedRoot.rootNodeId(), rootPaint));
    UI::UIRadioButtonPaint radioPaint{};
    radioPaint.focusedIndicatorColor = {
        .red = 40,
        .green = 50,
        .blue = 60,
        .alpha = 255,
    };
    assertOk(limitedUpdater.setRadioButtonPaint(radioButton, radioPaint));
    assertOk(limitedContext->commitLayout({.width = 40.0F, .height = 24.0F}));
    ASSERT_EQ(limitedContext->committedPaint().size(), 1U);
    const u64 publishedRevision = limitedContext->committedPaint().paintRevision();

    auto focus = limitedContext->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_TRUE(focus->consumed);
    EXPECT_EQ(focus->focus, radioButton);
    const Core::Status overflow =
        limitedContext->commitLayout({.width = 40.0F, .height = 24.0F});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(limitedContext->committedPaint().paintRevision(), publishedRevision);
    ASSERT_EQ(limitedContext->committedPaint().size(), 1U);
    EXPECT_EQ(limitedContext->committedPaint().entries()[0].node, limitedRoot.rootNodeId());
    EXPECT_TRUE(limitedContext->statistics().paintDirty);
}

TEST_F(UIRadioButtonTest, PointerAndKeyboardActivationSelectWithoutTogglingOff)
{
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 80.0F)));
    const UI::UINodeId first = createRadioButton(40.0F, 30.0F);
    const UI::UINodeId second = createRadioButton(40.0F, 30.0F);
    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    ActivationLog firstLog{};
    ActivationLog secondLog{};
    assertOk(updater.setRadioButtonAction(first, actionFor(firstLog)));
    assertOk(updater.setRadioButtonAction(second, actionFor(secondLog)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 80.0F}));

    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(down->pointQuery.target.node, first);
    EXPECT_TRUE(isPressed(updater, first));

    auto up = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(isPressed(updater, first));
    EXPECT_TRUE(isSelected(updater, first));
    EXPECT_FALSE(isSelected(updater, second));
    EXPECT_EQ(firstLog.count, 1);
    EXPECT_EQ(firstLog.node, first);
    EXPECT_EQ(firstLog.source, UI::UIButtonActivationSource::PrimaryPointer);
    EXPECT_EQ(firstLog.platformFrame, Platform::PlatformFrameId{2});
    EXPECT_EQ(firstLog.sourceSequence, 2U);
    EXPECT_EQ(context->defaultActionFocus(), first);

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_TRUE(focus->consumed);
    EXPECT_EQ(focus->focus, second);
    auto activation = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{3},
        3,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(activation.has_value())
        << (activation ? "" : activation.error().message);
    EXPECT_TRUE(activation->consumed);
    EXPECT_TRUE(activation->activated);
    EXPECT_FALSE(isSelected(updater, first));
    EXPECT_TRUE(isSelected(updater, second));
    EXPECT_EQ(secondLog.count, 1);
    EXPECT_EQ(secondLog.node, second);
    EXPECT_EQ(secondLog.source, UI::UIButtonActivationSource::Keyboard);
    EXPECT_EQ(secondLog.platformFrame, Platform::PlatformFrameId{3});
    EXPECT_EQ(secondLog.sourceSequence, 3U);

    assertOk(context->commitLayout({.width = 100.0F, .height = 80.0F}));
    const UI::UISemanticsEntry* const firstSemantics =
        findSemanticsEntry(context->committedSemantics(), first);
    const UI::UISemanticsEntry* const secondSemantics =
        findSemanticsEntry(context->committedSemantics(), second);
    ASSERT_NE(firstSemantics, nullptr);
    ASSERT_NE(secondSemantics, nullptr);
    EXPECT_FALSE(firstSemantics->checked);
    EXPECT_FALSE(firstSemantics->focused);
    EXPECT_TRUE(secondSemantics->checked);
    EXPECT_TRUE(secondSemantics->focused);

    auto repeat = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{4},
        4,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(repeat.has_value()) << (repeat ? "" : repeat.error().message);
    EXPECT_TRUE(repeat->activated);
    EXPECT_TRUE(isSelected(updater, second));
    EXPECT_EQ(secondLog.count, 2);

    auto gamepad = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{5},
        5,
        UI::UIButtonActivationSource::Gamepad);
    ASSERT_TRUE(gamepad.has_value()) << (gamepad ? "" : gamepad.error().message);
    EXPECT_TRUE(gamepad->consumed);
    EXPECT_TRUE(gamepad->activated);
    EXPECT_TRUE(isSelected(updater, second));
    EXPECT_EQ(secondLog.count, 3);
    EXPECT_EQ(secondLog.source, UI::UIButtonActivationSource::Gamepad);
    EXPECT_EQ(secondLog.platformFrame, Platform::PlatformFrameId{5});
    EXPECT_EQ(secondLog.sourceSequence, 5U);
}

TEST_F(UIRadioButtonTest, PointerRouteReservationSurvivesListenerSiblingMutation)
{
    auto limitedContext = createContext(
        window,
        {
            .nodeCapacity = 6,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 4,
            .paintSnapshotCapacity = 6,
            .routePathCapacity = 6,
            .routedPointerListenerCapacity = 1,
            .buttonActionCapacity = 3,
            .textByteCapacity = 64,
        });
    ASSERT_NE(limitedContext, nullptr);
    auto limitedRoot = createRoot(*limitedContext);
    ASSERT_TRUE(limitedRoot.hasValue());
    auto limitedUpdater = createUpdater(*limitedContext, limitedRoot);

    auto firstResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makeRadioButtonElement());
    auto secondResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makeRadioButtonElement());
    auto thirdResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makeRadioButtonElement());
    auto firstBlockerResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makePanelElement());
    auto secondBlockerResult = limitedUpdater.createElement(limitedRoot.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(firstResult.has_value());
    ASSERT_TRUE(secondResult.has_value());
    ASSERT_TRUE(thirdResult.has_value());
    ASSERT_TRUE(firstBlockerResult.has_value());
    ASSERT_TRUE(secondBlockerResult.has_value());
    const UI::UINodeId first = *firstResult;
    const UI::UINodeId second = *secondResult;
    const UI::UINodeId third = *thirdResult;
    const UI::UINodeId firstBlocker = *firstBlockerResult;
    const UI::UINodeId secondBlocker = *secondBlockerResult;

    assertOk(limitedUpdater.setLayoutStyle(
        limitedRoot.rootNodeId(),
        fixedSize(100.0F, 80.0F)));
    assertOk(limitedUpdater.setLayoutStyle(first, fixedSize(40.0F, 30.0F)));
    assertOk(limitedUpdater.setLayoutStyle(second, fixedSize(40.0F, 30.0F)));
    assertOk(limitedUpdater.setLayoutStyle(third, fixedSize(40.0F, 30.0F)));
    assertOk(limitedUpdater.setRadioButtonSelected(second, true));
    ActivationLog firstLog{};
    ActivationLog thirdLog{};
    assertOk(limitedUpdater.setRadioButtonAction(first, actionFor(firstLog)));
    assertOk(limitedUpdater.setRadioButtonAction(third, actionFor(thirdLog)));
    assertOk(limitedContext->commitLayout({.width = 100.0F, .height = 80.0F}));

    struct ListenerState final {
        UI::UIContext* context = nullptr;
        UI::UITreeUpdater* updater = nullptr;
        UI::UINodeId sibling{};
        UI::UINodeId firstBlocker{};
        UI::UINodeId secondBlocker{};
        bool invoked = false;
        bool siblingMutationSucceeded = false;
        bool commitRejected = false;
        bool firstBlockerSucceeded = false;
        bool secondBlockerRejected = false;
    } listenerState{
        .context = limitedContext.get(),
        .updater = &limitedUpdater,
        .sibling = third,
        .firstBlocker = firstBlocker,
        .secondBlocker = secondBlocker,
    };
    auto listenerResult = limitedContext->addRoutedPointerListener(
        {
            .node = first,
            .kind = UI::UIRoutedPointerEventKind::ButtonUp,
            .phases = UI::UIEventPhaseMask::Target,
        },
        UI::UIRoutedPointerCallback{
            [&listenerState](UI::UIRoutedPointerEvent&) noexcept {
                listenerState.invoked = true;
                const Core::Status siblingMutation =
                    listenerState.updater->setRadioButtonSelected(
                        listenerState.sibling,
                        true);
                listenerState.siblingMutationSucceeded =
                    siblingMutation.has_value();
                if (!listenerState.siblingMutationSucceeded) {
                    return;
                }
                const Core::Status listenerCommit =
                    listenerState.context->commitLayout(
                    {.width = 100.0F, .height = 80.0F});
                listenerState.commitRejected = !listenerCommit
                    && listenerCommit.error().code
                        == UI::UIErrorCode::PointerRouteAlreadyInProgress;

                const UI::UIBoxPaint blockerPaint{
                    .solidFill = UI::UISolidFill{
                        .color = {.red = 9, .green = 8, .blue = 7, .alpha = 255},
                    },
                };
                listenerState.firstBlockerSucceeded =
                    listenerState.updater->setBoxPaint(
                        listenerState.firstBlocker,
                        blockerPaint).has_value();
                const Core::Status secondMutation =
                    listenerState.updater->setBoxPaint(
                        listenerState.secondBlocker,
                        blockerPaint);
                listenerState.secondBlockerRejected = !secondMutation
                    && secondMutation.error().code
                        == UI::UIErrorCode::CapacityExceeded;
            }});
    ASSERT_TRUE(listenerResult.has_value())
        << (listenerResult ? "" : listenerResult.error().message);
    auto listenerToken = std::move(*listenerResult);

    auto down = limitedContext->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(isPressed(limitedUpdater, first));
    assertOk(limitedContext->commitLayout({.width = 100.0F, .height = 80.0F}));

    auto up = limitedContext->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(listenerState.invoked);
    EXPECT_TRUE(listenerState.siblingMutationSucceeded);
    EXPECT_TRUE(listenerState.commitRejected);
    EXPECT_TRUE(listenerState.firstBlockerSucceeded);
    EXPECT_TRUE(listenerState.secondBlockerRejected);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(isPressed(limitedUpdater, first));
    EXPECT_TRUE(isSelected(limitedUpdater, first));
    EXPECT_FALSE(isSelected(limitedUpdater, second));
    EXPECT_FALSE(isSelected(limitedUpdater, third));
    EXPECT_EQ(firstLog.count, 1);
    EXPECT_EQ(thirdLog.count, 0);
    EXPECT_EQ(limitedContext->statistics().dirtyQueuePendingCount, 4U);

    // The listener's re-entrant commit is rejected; the initial committed
    // selection remains published until the route's pending action commits.
    const UI::UISemanticsEntry* committedFirst = findSemanticsEntry(
        limitedContext->committedSemantics(),
        first);
    const UI::UISemanticsEntry* committedSecond = findSemanticsEntry(
        limitedContext->committedSemantics(),
        second);
    const UI::UISemanticsEntry* committedThird = findSemanticsEntry(
        limitedContext->committedSemantics(),
        third);
    ASSERT_NE(committedFirst, nullptr);
    ASSERT_NE(committedSecond, nullptr);
    ASSERT_NE(committedThird, nullptr);
    EXPECT_FALSE(committedFirst->checked);
    EXPECT_TRUE(committedSecond->checked);
    EXPECT_FALSE(committedThird->checked);

    assertOk(limitedContext->commitLayout({.width = 100.0F, .height = 80.0F}));
    committedFirst = findSemanticsEntry(limitedContext->committedSemantics(), first);
    committedSecond = findSemanticsEntry(limitedContext->committedSemantics(), second);
    committedThird = findSemanticsEntry(limitedContext->committedSemantics(), third);
    ASSERT_NE(committedFirst, nullptr);
    ASSERT_NE(committedSecond, nullptr);
    ASSERT_NE(committedThird, nullptr);
    EXPECT_TRUE(committedFirst->checked);
    EXPECT_FALSE(committedSecond->checked);
    EXPECT_FALSE(committedThird->checked);
}

TEST_F(UIRadioButtonTest, RejectsInvalidPaintMultilineTextAndWrongKinds)
{
    const UI::UINodeId radioButton = createRadioButton();
    ASSERT_TRUE(radioButton.hasValue());
    auto buttonResult = updater.createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(buttonResult.has_value()) << (buttonResult ? "" : buttonResult.error().message);
    const UI::UINodeId button = *buttonResult;

    const UI::UIRadioButtonPaint expectedPaint{
        .indicatorColor = {.red = 1, .green = 2, .blue = 3, .alpha = 4},
        .selectedIndicatorColor = {.red = 5, .green = 6, .blue = 7, .alpha = 8},
        .selectedIndicatorInset = 3.0F,
        .labelGap = 9.0F,
    };
    assertOk(updater.setRadioButtonPaint(radioButton, expectedPaint));
    assertOk(updater.setText(radioButton, "Option A"));

    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    const float infinity = (std::numeric_limits<float>::infinity)();
    UI::UIRadioButtonPaint invalidPaint = expectedPaint;
    invalidPaint.selectedIndicatorInset = -1.0F;
    expectInvalidControlValue(updater.setRadioButtonPaint(radioButton, invalidPaint));
    invalidPaint = expectedPaint;
    invalidPaint.selectedIndicatorInset = infinity;
    expectInvalidControlValue(updater.setRadioButtonPaint(radioButton, invalidPaint));
    invalidPaint = expectedPaint;
    invalidPaint.labelGap = -1.0F;
    expectInvalidControlValue(updater.setRadioButtonPaint(radioButton, invalidPaint));
    invalidPaint = expectedPaint;
    invalidPaint.labelGap = nan;
    expectInvalidControlValue(updater.setRadioButtonPaint(radioButton, invalidPaint));

    const Core::Status newline = updater.setText(radioButton, "Option\nB");
    ASSERT_FALSE(newline.has_value());
    EXPECT_EQ(newline.error().code, UI::UIErrorCode::InvalidText);
    const Core::Status carriageReturn = updater.setText(radioButton, "Option\rB");
    ASSERT_FALSE(carriageReturn.has_value());
    EXPECT_EQ(carriageReturn.error().code, UI::UIErrorCode::InvalidText);

    expectInvalidControlValue(updater.setRadioButtonPaint(button, expectedPaint));
    expectInvalidControlValue(updater.setRadioButtonSelected(button, true));
    ActivationLog wrongKindLog{};
    expectInvalidControlValue(updater.setRadioButtonAction(button, actionFor(wrongKindLog)));
    expectInvalidControlValue(updater.clearRadioButtonAction(button));
    auto wrongPaint = updater.radioButtonPaint(button);
    ASSERT_FALSE(wrongPaint.has_value());
    EXPECT_EQ(wrongPaint.error().code, UI::UIErrorCode::InvalidControlValue);
    auto wrongSelected = updater.isRadioButtonSelected(button);
    ASSERT_FALSE(wrongSelected.has_value());
    EXPECT_EQ(wrongSelected.error().code, UI::UIErrorCode::InvalidControlValue);
    auto wrongPressed = updater.isRadioButtonPressed(button);
    ASSERT_FALSE(wrongPressed.has_value());
    EXPECT_EQ(wrongPressed.error().code, UI::UIErrorCode::InvalidControlValue);

    auto paint = updater.radioButtonPaint(radioButton);
    ASSERT_TRUE(paint.has_value());
    EXPECT_EQ(*paint, expectedPaint);
    auto text = updater.text(radioButton);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "Option A");
}

TEST_F(UIRadioButtonTest, TypedWrappersRejectOffThreadBeforeKindResolution)
{
    auto buttonResult = updater.createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(buttonResult.has_value())
        << (buttonResult ? "" : buttonResult.error().message);
    const UI::UINodeId wrongKind = *buttonResult;

    std::array<bool, 3> succeeded{};
    std::array<Core::ErrorCode, 3> errors{};
    std::thread worker([&]() {
        auto setAction = updater.setRadioButtonAction(
            wrongKind,
            UI::UIButtonActionCallback{
                [](const UI::UIButtonActionEvent&) noexcept {}});
        succeeded[0] = setAction.has_value();
        errors[0] = setAction ? Core::ErrorCode{} : setAction.error().code;

        auto clearAction = updater.clearRadioButtonAction(wrongKind);
        succeeded[1] = clearAction.has_value();
        errors[1] = clearAction ? Core::ErrorCode{} : clearAction.error().code;

        auto pressed = updater.isRadioButtonPressed(wrongKind);
        succeeded[2] = pressed.has_value();
        errors[2] = pressed ? Core::ErrorCode{} : pressed.error().code;
    });
    worker.join();

    for (usize index = 0; index < succeeded.size(); ++index) {
        EXPECT_FALSE(succeeded[index]);
        EXPECT_EQ(errors[index], UI::UIErrorCode::WrongOwnerThread);
    }
}

} // namespace
} // namespace Tina::Tests
