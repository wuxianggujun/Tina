#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

constexpr UI::UIStraightSrgba8Color NormalColor{
    .red = 20,
    .green = 40,
    .blue = 60,
    .alpha = 255,
};
constexpr UI::UIStraightSrgba8Color HoveredColor{
    .red = 40,
    .green = 80,
    .blue = 120,
    .alpha = 255,
};
constexpr UI::UIStraightSrgba8Color PressedColor{
    .red = 80,
    .green = 120,
    .blue = 160,
    .alpha = 255,
};
constexpr UI::UIStraightSrgba8Color FocusedColor{
    .red = 120,
    .green = 160,
    .blue = 200,
    .alpha = 255,
};
constexpr UI::UIStraightSrgba8Color DisabledColor{
    .red = 255,
    .green = 255,
    .blue = 255,
    .alpha = 255,
};

constexpr UI::UIButtonPaint StatePaint{
    .hoveredBackgroundColor = HoveredColor,
    .pressedBackgroundColor = PressedColor,
    .focusedBackgroundColor = FocusedColor,
    .disabledBackgroundColor = DisabledColor,
};

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 8,
        .buttonActionCapacity = 8,
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

[[nodiscard]] UI::UINodeId createButton(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createElement(parent, UI::makeButtonElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createPanel(
    UI::UIContext& context,
    UI::UINodeId parent)
{
    auto result = context.rootBuilder().createElement(parent, UI::makePanelElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
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
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

[[nodiscard]] UI::UIBoxPaint solidFill(
    UI::UIStraightSrgba8Color color) noexcept
{
    return UI::UIBoxPaint{.solidFill = UI::UISolidFill{.color = color}};
}

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

void publishLayout(
    UI::UIContext& context,
    float width = 100.0F,
    float height = 100.0F)
{
    assertOk(context.commitLayout({.width = width, .height = height}));
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
            ? UI::UILogicalPoint{.x = 1.0F, .y = 0.0F}
            : UI::UILogicalPoint{},
        .button = Platform::PointerButton::Primary,
    };
}

[[nodiscard]] UI::UIPointerRouteResult route(
    UI::UIContext& context,
    const UI::UIPointerInputEvent& input)
{
    auto result = context.routePointerInput(input);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UIPointerRouteResult{};
}

[[nodiscard]] const UI::UICommittedPaintEntry* findPaintEntry(
    UI::UICommittedPaintView paint,
    UI::UINodeId node) noexcept
{
    for (const UI::UICommittedPaintEntry& entry : paint.entries()) {
        if (entry.node == node && entry.kind != UI::UICommittedPaintKind::Glyph) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] usize countSolidEntries(
    UI::UICommittedPaintView paint,
    UI::UINodeId node) noexcept
{
    usize count = 0;
    for (const UI::UICommittedPaintEntry& entry : paint.entries()) {
        if (entry.node == node && entry.kind != UI::UICommittedPaintKind::Glyph) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool hasSolidColor(
    UI::UICommittedPaintView paint,
    UI::UINodeId node,
    UI::UIStraightSrgba8Color color) noexcept
{
    const UI::UIPremultipliedRgba8Color expected = UI::premultiply(color);
    for (const UI::UICommittedPaintEntry& entry : paint.entries()) {
        if (entry.node == node && entry.kind != UI::UICommittedPaintKind::Glyph && entry.solidFill == expected) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const UI::UISemanticsEntry* findSemanticsEntry(
    UI::UICommittedSemanticsView semantics,
    UI::UINodeId node) noexcept
{
    for (const UI::UISemanticsEntry& entry : semantics.entries()) {
        if (entry.node == node) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] const UI::UICommittedLayoutEntry* findLayoutEntry(
    UI::UICommittedLayoutView layout,
    UI::UINodeId node) noexcept
{
    for (const UI::UICommittedLayoutEntry& entry : layout.entries()) {
        if (entry.node == node) {
            return &entry;
        }
    }
    return nullptr;
}

void expectButtonFill(
    const UI::UIContext& context,
    UI::UINodeId button,
    UI::UIPremultipliedRgba8Color expected)
{
    const UI::UICommittedPaintEntry* entry =
        findPaintEntry(context.committedPaint(), button);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->solidFill, expected);
}

void expectButtonFill(
    const UI::UIContext& context,
    UI::UINodeId button,
    UI::UIStraightSrgba8Color expected)
{
    expectButtonFill(context, button, UI::premultiply(expected));
}

[[nodiscard]] bool isButtonPressed(
    UI::UITreeUpdater& updater,
    UI::UINodeId button)
{
    auto result = updater.isButtonPressed(button);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : false;
}

TEST(UIButtonVisualTest, PaintTracksHoverPressMoveReleaseAndFocus)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    ASSERT_TRUE(button.hasValue());
    auto updater = createUpdater(*context, root);

    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(button, solidFill(NormalColor)));
    assertOk(updater.setButtonPaint(button, StatePaint));
    int activations = 0;
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout(*context);

    expectButtonFill(*context, button, NormalColor);
    EXPECT_FALSE(context->defaultActionFocus().hasValue());

    const UI::UIPointerRouteResult hover = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::Move,
            1,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_EQ(hover.pointQuery.target.node, button);
    EXPECT_FALSE(hover.consumed);
    publishLayout(*context);
    expectButtonFill(*context, button, HoveredColor);

    const UI::UIPointerRouteResult down = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonDown,
            2,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_TRUE(down.consumed);
    EXPECT_TRUE(isButtonPressed(updater, button));
    EXPECT_EQ(context->defaultActionFocus(), button);
    publishLayout(*context);
    expectButtonFill(*context, button, PressedColor);
    const UI::UISemanticsEntry* semantics =
        findSemanticsEntry(context->committedSemantics(), button);
    ASSERT_NE(semantics, nullptr);
    EXPECT_TRUE(semantics->focused);

    (void)route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::Move,
            3,
            {.x = 90.0F, .y = 90.0F}));
    EXPECT_FALSE(isButtonPressed(updater, button));
    publishLayout(*context);
    expectButtonFill(*context, button, NormalColor);

    (void)route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::Move,
            4,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_TRUE(isButtonPressed(updater, button));
    publishLayout(*context);
    expectButtonFill(*context, button, PressedColor);

    const UI::UIPointerRouteResult up = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonUp,
            5,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_TRUE(up.consumed);
    EXPECT_FALSE(isButtonPressed(updater, button));
    EXPECT_EQ(activations, 1);
    publishLayout(*context);
    expectButtonFill(*context, button, HoveredColor);

    (void)route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::Move,
            6,
            {.x = 90.0F, .y = 90.0F}));
    publishLayout(*context);
    expectButtonFill(*context, button, NormalColor);
    semantics = findSemanticsEntry(context->committedSemantics(), button);
    ASSERT_NE(semantics, nullptr);
    EXPECT_TRUE(semantics->focused);
}

TEST(UIButtonVisualTest, ProductChromeUsesSemanticElevationAndPublishesFocusBorder)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto contextResult = UI::UIContext::Create(
        window,
        UI::UIContextCapacityConfig{
            .nodeCapacity = 16,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 16,
            .routePathCapacity = 8,
            .buttonActionCapacity = 8,
        });
    ASSERT_TRUE(contextResult.has_value());
    std::unique_ptr<UI::UIContext> context = std::move(*contextResult);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(60.0F, 36.0F)));
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [](const UI::UIButtonActionEvent&) noexcept {}}));
    publishLayout(*context);

    const UI::UIButtonChrome chrome = UI::makeTonalButtonChrome(context->productTheme());
    EXPECT_EQ(countSolidEntries(context->committedPaint(), button), 2U);
    EXPECT_TRUE(hasSolidColor(context->committedPaint(), button, chrome.box.shadow));

    const UI::UIPointerRouteResult downRoute = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonDown,
            1,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_TRUE(downRoute.consumed);
    publishLayout(*context);
    EXPECT_EQ(countSolidEntries(context->committedPaint(), button), 1U);
    EXPECT_FALSE(hasSolidColor(context->committedPaint(), button, chrome.box.shadow));
    EXPECT_TRUE(hasSolidColor(
        context->committedPaint(),
        button,
        chrome.states.pressedBackgroundColor));

    const UI::UIPointerRouteResult upRoute = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonUp,
            2,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_TRUE(upRoute.consumed);
    publishLayout(*context);
    EXPECT_EQ(countSolidEntries(context->committedPaint(), button), 2U);
    EXPECT_FALSE(hasSolidColor(
        context->committedPaint(),
        button,
        chrome.states.focusedBorderColor));

    const UI::UISemanticsEntry* semantics =
        findSemanticsEntry(context->committedSemantics(), button);
    ASSERT_NE(semantics, nullptr);
    EXPECT_TRUE(semantics->focused);

    auto navigation = context->routeFocusNavigation(
        UI::UIFocusNavigationDirection::Right, true, UI::UIInputModality::Keyboard);
    ASSERT_TRUE(navigation.has_value()) << navigation.error().message;
    publishLayout(*context);
    EXPECT_TRUE(hasSolidColor(
        context->committedPaint(),
        button,
        chrome.states.focusedBorderColor));
}

TEST(UIButtonVisualTest, KeyboardAcceptDownAndUpCommitPressedAndFocusedPaint)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    ASSERT_TRUE(button.hasValue());
    auto updater = createUpdater(*context, root);

    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(button, solidFill(NormalColor)));
    assertOk(updater.setButtonPaint(button, StatePaint));
    publishLayout(*context);

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_TRUE(focus->moved);
    ASSERT_EQ(focus->focus, button);
    publishLayout(*context);
    expectButtonFill(*context, button, FocusedColor);

    const Platform::DigitalControlIdentity enter =
        Platform::KeyControlIdentity{window, Platform::Key::Enter};
    auto down = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        10,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(isButtonPressed(updater, button));
    publishLayout(*context);
    expectButtonFill(*context, button, PressedColor);

    auto up = context->routeDefaultActionRelease(
        Platform::PlatformFrameId{2},
        20,
        UI::UIButtonActivationSource::Keyboard,
        enter);
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(isButtonPressed(updater, button));
    publishLayout(*context);
    expectButtonFill(*context, button, FocusedColor);
}

TEST(UIButtonVisualTest, DisabledPaintWinsAndDisabledButtonDoesNotInteract)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(button, solidFill(NormalColor)));
    assertOk(updater.setButtonPaint(button, StatePaint));
    int activations = 0;
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout(*context);

    (void)route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonDown,
            1,
            {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(isButtonPressed(updater, button));
    ASSERT_EQ(context->defaultActionFocus(), button);
    assertOk(updater.setEnabled(button, false));
    EXPECT_FALSE(isButtonPressed(updater, button));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    publishLayout(*context);

    expectButtonFill(
        *context,
        button,
        UI::UIPremultipliedRgba8Color{
            .red = 140,
            .green = 140,
            .blue = 140,
            .alpha = 140,
        });
    const UI::UISemanticsEntry* semantics =
        findSemanticsEntry(context->committedSemantics(), button);
    ASSERT_NE(semantics, nullptr);
    EXPECT_FALSE(semantics->enabled);
    EXPECT_FALSE(semantics->focused);

    const UI::UIPointerRouteResult move = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::Move,
            2,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_FALSE(move.consumed);
    const UI::UIPointerRouteResult down = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonDown,
            3,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_FALSE(down.consumed);
    const UI::UIPointerRouteResult up = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonUp,
            4,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_FALSE(up.consumed);
    EXPECT_FALSE(isButtonPressed(updater, button));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_EQ(activations, 0);
    publishLayout(*context);
    expectButtonFill(
        *context,
        button,
        UI::UIPremultipliedRgba8Color{
            .red = 140,
            .green = 140,
            .blue = 140,
            .alpha = 140,
        });
}

TEST(UIButtonVisualTest, CancelClearsPressedHoverFocusAndDoesNotSynthesizeUp)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 5,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 2,
            .paintSnapshotCapacity = 5,
            .routePathCapacity = 5,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    const UI::UINodeId firstBlocker = createPanel(*context, root.rootNodeId());
    const UI::UINodeId secondBlocker = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(button, solidFill(NormalColor)));
    assertOk(updater.setButtonPaint(button, StatePaint));
    int activations = 0;
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout(*context);

    (void)route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonDown,
            1,
            {.x = 10.0F, .y = 10.0F}));
    publishLayout(*context);
    expectButtonFill(*context, button, PressedColor);
    ASSERT_TRUE(isButtonPressed(updater, button));
    ASSERT_EQ(context->defaultActionFocus(), button);

    assertOk(updater.setBoxPaint(
        firstBlocker,
        solidFill({.red = 1, .green = 2, .blue = 3, .alpha = 255})));
    assertOk(updater.setBoxPaint(
        secondBlocker,
        solidFill({.red = 4, .green = 5, .blue = 6, .alpha = 255})));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 2U);
    assertOk(context->cancelPointerInteraction(window));
    EXPECT_FALSE(isButtonPressed(updater, button));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    EXPECT_EQ(activations, 0);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 2U);
    publishLayout(*context);
    expectButtonFill(*context, button, NormalColor);

    const UI::UIPointerRouteResult up = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonUp,
            2,
            {.x = 90.0F, .y = 90.0F}));
    EXPECT_FALSE(up.consumed);
    EXPECT_EQ(activations, 0);

    const u64 paintRevision = context->committedPaint().paintRevision();
    assertOk(context->cancelPointerInteraction(window));
    publishLayout(*context);
    EXPECT_EQ(context->committedPaint().paintRevision(), paintRevision);
    expectButtonFill(*context, button, NormalColor);
}

TEST(UIButtonVisualTest, HiddenOrCollapsedSelfClearsInteractionBeforeRestore)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    const UI::UILayoutStyle visible = fixedSize(40.0F, 40.0F);
    assertOk(updater.setLayoutStyle(button, visible));
    assertOk(updater.setBoxPaint(button, solidFill(NormalColor)));
    assertOk(updater.setButtonPaint(button, StatePaint));
    int activations = 0;
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout(*context);

    u64 sequence = 1;
    for (const UI::UIVisibility visibility : {
             UI::UIVisibility::Hidden,
             UI::UIVisibility::Collapsed,
         }) {
        (void)route(
            *context,
            makePointerInput(
                window,
                UI::UIRoutedPointerEventKind::ButtonDown,
                sequence++,
                {.x = 10.0F, .y = 10.0F}));
        ASSERT_TRUE(isButtonPressed(updater, button));
        ASSERT_EQ(context->defaultActionFocus(), button);
        publishLayout(*context);
        expectButtonFill(*context, button, PressedColor);

        UI::UILayoutStyle unavailable = visible;
        unavailable.visibility = visibility;
        assertOk(updater.setLayoutStyle(button, unavailable));
        publishLayout(*context);
        EXPECT_FALSE(isButtonPressed(updater, button));
        EXPECT_FALSE(context->defaultActionFocus().hasValue());
        EXPECT_EQ(findPaintEntry(context->committedPaint(), button), nullptr);
        EXPECT_EQ(findSemanticsEntry(context->committedSemantics(), button), nullptr);
        const UI::UICommittedLayoutEntry* layout =
            findLayoutEntry(context->committedLayout(), button);
        ASSERT_NE(layout, nullptr);
        EXPECT_EQ(layout->effectiveVisibility, visibility);

        const UI::UIPointerRouteResult up = route(
            *context,
            makePointerInput(
                window,
                UI::UIRoutedPointerEventKind::ButtonUp,
                sequence++,
                {.x = 10.0F, .y = 10.0F}));
        EXPECT_FALSE(up.consumed);
        EXPECT_EQ(activations, 0);

        assertOk(updater.setLayoutStyle(button, visible));
        publishLayout(*context);
        EXPECT_FALSE(isButtonPressed(updater, button));
        EXPECT_FALSE(context->defaultActionFocus().hasValue());
        expectButtonFill(*context, button, NormalColor);
        const UI::UISemanticsEntry* semantics =
            findSemanticsEntry(context->committedSemantics(), button);
        ASSERT_NE(semantics, nullptr);
        EXPECT_FALSE(semantics->focused);
    }
}

TEST(UIButtonVisualTest, HiddenOrCollapsedAncestorClearsInteractionBeforeRestore)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    const UI::UINodeId button = createButton(*context, panel);
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    const UI::UILayoutStyle visiblePanel = fixedSize(80.0F, 80.0F);
    assertOk(updater.setLayoutStyle(panel, visiblePanel));
    assertOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(button, solidFill(NormalColor)));
    assertOk(updater.setButtonPaint(button, StatePaint));
    publishLayout(*context);

    u64 sequence = 1;
    for (const UI::UIVisibility visibility : {
             UI::UIVisibility::Hidden,
             UI::UIVisibility::Collapsed,
         }) {
        (void)route(
            *context,
            makePointerInput(
                window,
                UI::UIRoutedPointerEventKind::ButtonDown,
                sequence++,
                {.x = 10.0F, .y = 10.0F}));
        ASSERT_TRUE(isButtonPressed(updater, button));
        ASSERT_EQ(context->defaultActionFocus(), button);
        publishLayout(*context);

        UI::UILayoutStyle unavailable = visiblePanel;
        unavailable.visibility = visibility;
        assertOk(updater.setLayoutStyle(panel, unavailable));
        publishLayout(*context);
        EXPECT_FALSE(isButtonPressed(updater, button));
        EXPECT_FALSE(context->defaultActionFocus().hasValue());
        EXPECT_EQ(findPaintEntry(context->committedPaint(), button), nullptr);
        EXPECT_EQ(findSemanticsEntry(context->committedSemantics(), button), nullptr);
        const UI::UICommittedLayoutEntry* layout =
            findLayoutEntry(context->committedLayout(), button);
        ASSERT_NE(layout, nullptr);
        EXPECT_EQ(layout->effectiveVisibility, visibility);

        assertOk(updater.setLayoutStyle(panel, visiblePanel));
        publishLayout(*context);
        EXPECT_FALSE(isButtonPressed(updater, button));
        EXPECT_FALSE(context->defaultActionFocus().hasValue());
        expectButtonFill(*context, button, NormalColor);
        const UI::UISemanticsEntry* semantics =
            findSemanticsEntry(context->committedSemantics(), button);
        ASSERT_NE(semantics, nullptr);
        EXPECT_FALSE(semantics->focused);
    }
}

TEST(UIButtonVisualTest, PaintApiRejectsWrongKindCrossRootCrossContextAndStaleIds)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 8,
            .rootCapacity = 2,
            .routePathCapacity = 8,
            .buttonActionCapacity = 8,
        });
    ASSERT_NE(context, nullptr);
    auto firstRoot = createRoot(*context);
    auto secondRoot = createRoot(*context);
    ASSERT_TRUE(firstRoot && secondRoot);
    const UI::UINodeId button =
        createButton(*context, firstRoot.rootNodeId());
    const UI::UINodeId panel =
        createPanel(*context, firstRoot.rootNodeId());
    const UI::UINodeId stale =
        createButton(*context, firstRoot.rootNodeId());
    const UI::UINodeId otherRootButton =
        createButton(*context, secondRoot.rootNodeId());
    auto updater = createUpdater(*context, firstRoot);
    assertOk(updater.setButtonPaint(button, StatePaint));
    auto current = updater.buttonPaint(button);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(*current, StatePaint);

    auto foreignContext = createContext(window);
    ASSERT_NE(foreignContext, nullptr);
    auto foreignRoot = createRoot(*foreignContext);
    ASSERT_TRUE(foreignRoot);
    const UI::UINodeId foreignButton =
        createButton(*foreignContext, foreignRoot.rootNodeId());

    const auto expectError = [](const auto& result, Core::ErrorCode expected) {
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, expected);
    };

    expectError(
        updater.setButtonPaint(panel, StatePaint),
        UI::UIErrorCode::InvalidButtonAction);
    expectError(
        updater.buttonPaint(panel),
        UI::UIErrorCode::InvalidButtonAction);
    expectError(
        updater.setButtonPaint(otherRootButton, StatePaint),
        UI::UIErrorCode::InvalidNode);
    expectError(
        updater.buttonPaint(otherRootButton),
        UI::UIErrorCode::InvalidNode);
    expectError(
        updater.setButtonPaint(foreignButton, StatePaint),
        UI::UIErrorCode::WrongContext);
    expectError(
        updater.buttonPaint(foreignButton),
        UI::UIErrorCode::WrongContext);

    assertOk(updater.destroy(stale));
    expectError(
        updater.setButtonPaint(stale, StatePaint),
        UI::UIErrorCode::InvalidNode);
    expectError(
        updater.buttonPaint(stale),
        UI::UIErrorCode::InvalidNode);

    current = updater.buttonPaint(button);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(*current, StatePaint);
}

TEST(UIButtonVisualTest, DirtyQueueFailurePreservesButtonPaintMutation)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 5,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 2,
            .paintSnapshotCapacity = 5,
            .routePathCapacity = 5,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    const UI::UINodeId firstBlocker = createPanel(*context, root.rootNodeId());
    const UI::UINodeId secondBlocker = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(button, solidFill(NormalColor)));
    assertOk(updater.setButtonPaint(button, StatePaint));
    publishLayout(*context);

    const UI::UIButtonPaint replacement{
        .hoveredBackgroundColor = {.red = 1, .green = 2, .blue = 3, .alpha = 255},
        .pressedBackgroundColor = {.red = 4, .green = 5, .blue = 6, .alpha = 255},
        .focusedBackgroundColor = {.red = 7, .green = 8, .blue = 9, .alpha = 255},
        .disabledBackgroundColor = {.red = 10, .green = 11, .blue = 12, .alpha = 255},
    };
    assertOk(updater.setBoxPaint(
        firstBlocker,
        solidFill({.red = 1, .green = 2, .blue = 3, .alpha = 255})));
    assertOk(updater.setBoxPaint(
        secondBlocker,
        solidFill({.red = 4, .green = 5, .blue = 6, .alpha = 255})));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 2U);

    const Core::Status rejected = updater.setButtonPaint(button, replacement);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    auto current = updater.buttonPaint(button);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(*current, StatePaint);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 2U);

    publishLayout(*context);
    expectButtonFill(*context, button, NormalColor);
}

TEST(UIButtonVisualTest, ButtonUpDirtyQueueFailureStillReleasesPressedState)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 5,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 2,
            .paintSnapshotCapacity = 5,
            .routePathCapacity = 5,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    const UI::UINodeId firstBlocker = createPanel(*context, root.rootNodeId());
    const UI::UINodeId secondBlocker = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(button, solidFill(NormalColor)));
    assertOk(updater.setButtonPaint(button, StatePaint));
    int activations = 0;
    assertOk(updater.setButtonAction(
        button,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout(*context);

    (void)route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonDown,
            1,
            {.x = 10.0F, .y = 10.0F}));
    publishLayout(*context);
    ASSERT_TRUE(isButtonPressed(updater, button));
    expectButtonFill(*context, button, PressedColor);

    assertOk(updater.setBoxPaint(
        firstBlocker,
        solidFill({.red = 1, .green = 2, .blue = 3, .alpha = 255})));
    assertOk(updater.setBoxPaint(
        secondBlocker,
        solidFill({.red = 4, .green = 5, .blue = 6, .alpha = 255})));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 2U);

    auto rejected = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_FALSE(isButtonPressed(updater, button));
    EXPECT_EQ(context->defaultActionFocus(), button);
    EXPECT_EQ(activations, 0);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 2U);

    publishLayout(*context);
    expectButtonFill(*context, button, HoveredColor);
    const UI::UIPointerRouteResult duplicateUp = route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::ButtonUp,
            3,
            {.x = 10.0F, .y = 10.0F}));
    EXPECT_FALSE(duplicateUp.consumed);
    EXPECT_EQ(activations, 0);
}

TEST(UIButtonVisualTest, StateOverrideParticipatesInPaintCapacityPreflight)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const Platform::WindowId window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 4,
            .paintSnapshotCapacity = 1,
            .routePathCapacity = 4,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root);
    const UI::UINodeId button = createButton(*context, root.rootNodeId());
    const UI::UINodeId panel = createPanel(*context, root.rootNodeId());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(button, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(panel, fixedSize(20.0F, 20.0F)));
    assertOk(updater.setButtonPaint(button, StatePaint));
    assertOk(updater.setBoxPaint(
        panel,
        solidFill({.red = 7, .green = 8, .blue = 9, .alpha = 255})));
    publishLayout(*context);

    const UI::UICommittedPaintView baseline = context->committedPaint();
    ASSERT_EQ(baseline.size(), 1U);
    ASSERT_EQ(baseline.entries()[0].node, panel);
    const UI::UICommittedPaintEntry* const oldEntries = baseline.entries().data();
    const u64 oldRevision = baseline.paintRevision();

    (void)route(
        *context,
        makePointerInput(
            window,
            UI::UIRoutedPointerEventKind::Move,
            1,
            {.x = 10.0F, .y = 10.0F}));
    const Core::Status overflow = context->commitLayout(
        {.width = 100.0F, .height = 100.0F});
    ASSERT_FALSE(overflow.has_value());
    EXPECT_EQ(overflow.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedPaint().entries().data(), oldEntries);
    EXPECT_EQ(context->committedPaint().paintRevision(), oldRevision);
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(context->committedPaint().entries()[0].node, panel);

    assertOk(updater.setBoxPaint(panel, {}));
    publishLayout(*context);
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(context->committedPaint().entries()[0].node, button);
    expectButtonFill(*context, button, HoveredColor);
}

} // namespace
} // namespace Tina::Tests
