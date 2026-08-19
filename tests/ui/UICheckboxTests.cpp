#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <memory_resource>
#include <thread>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;
using GamepadPool = Core::GenerationPool<int, Platform::GamepadRegistryTag>;

[[nodiscard]] std::unique_ptr<UI::UIContext> createContext(
    Platform::WindowId window,
    UI::UIContextCapacityConfig capacities = {
        .nodeCapacity = 8,
        .rootCapacity = 1,
        .routePathCapacity = 8,
        .routedPointerListenerCapacity = 8,
        .buttonActionCapacity = 8,
    },
    std::pmr::memory_resource& resource = *std::pmr::get_default_resource())
{
    capacities.applyDefaultProductChrome = false;
    auto result = UI::UIContext::Create(window, capacities, resource);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : nullptr;
}

[[nodiscard]] UI::UIRootOwner createRoot(UI::UIContext& context)
{
    auto result = context.rootBuilder().createRoot();
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? std::move(*result) : UI::UIRootOwner{};
}

[[nodiscard]] UI::UINodeId createCheckbox(UI::UIContext& context, UI::UINodeId parent)
{
    auto result = context.rootBuilder().createElement(parent, UI::makeCheckboxElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createRadioButton(UI::UIContext& context, UI::UINodeId parent)
{
    auto result = context.rootBuilder().createElement(parent, UI::makeRadioButtonElement());
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UITreeUpdater createUpdater(UI::UIContext& context, UI::UIRootOwner& root)
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
    u8 red,
    u8 green,
    u8 blue,
    u8 alpha = 255) noexcept
{
    return UI::UIBoxPaint{
        .solidFill = UI::UISolidFill{
            .color = {
                .red = red,
                .green = green,
                .blue = blue,
                .alpha = alpha,
            },
        },
    };
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

void assertOk(Core::Status status)
{
    ASSERT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

[[nodiscard]] UI::UIPointerInputEvent makePointerInput(
    Platform::WindowId window,
    UI::UIRoutedPointerEventKind kind,
    u64 sequence,
    UI::UILogicalPoint position = {.x = 10.0F, .y = 10.0F}) noexcept
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

void publishLayout(UI::UIContext& context, float width = 100.0F, float height = 100.0F)
{
    assertOk(context.commitLayout(UI::UILogicalSize{.width = width, .height = height}));
}

TEST(UICheckboxTest, CapabilitiesAreTargetableAndDefaultUnchecked)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value()) << (checked ? "" : checked.error().message);
    EXPECT_FALSE(*checked);

    assertOk(context->commitLayout({.width = 160.0F, .height = 80.0F}));
    const auto hit = context->committedHit();
    const auto entry = std::ranges::find_if(
        hit, [checkbox](const UI::UICommittedHitEntry& candidate) { return candidate.node == checkbox; });
    ASSERT_NE(entry, hit.end());
    EXPECT_EQ(entry->policy, UI::UIPointerHitPolicy::Targetable);
    EXPECT_TRUE(UI::hasBehavior(entry->behaviors, UI::UIElementBehavior::Focusable));
    EXPECT_TRUE(UI::hasBehavior(entry->behaviors, UI::UIElementBehavior::Activate));
    EXPECT_TRUE(UI::hasBehavior(entry->behaviors, UI::UIElementBehavior::Toggle));
}

TEST(UICheckboxTest, HoverDirtyCapacityFailurePreservesStateAtomically)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {.nodeCapacity = 4,
         .rootCapacity = 1,
         .dirtyQueueCapacity = 2,
         .routePathCapacity = 4});
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId checkbox =
        createCheckbox(*context, root.rootNodeId());
    auto blocker = context->rootBuilder().createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(checkbox.hasValue());
    ASSERT_TRUE(blocker.has_value());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(checkbox, solidFill(10, 20, 30)));
    assertOk(updater.setCheckboxPaint(
        checkbox,
        {
            .hoveredIndicatorColor = {.red = 40, .green = 50, .blue = 60, .alpha = 255},
        }));
    publishLayout(*context);
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{10, 20, 30, 255}));

    assertOk(updater.setBoxPaint(root.rootNodeId(), solidFill(1, 2, 3)));
    assertOk(updater.setBoxPaint(*blocker, solidFill(4, 5, 6)));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 2U);

    auto moved = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::Move,
        1));
    ASSERT_FALSE(moved.has_value());
    EXPECT_EQ(moved.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 2U);

    publishLayout(*context);
    ASSERT_EQ(context->committedPaint().size(), 3U);
    const UI::UICommittedPaintView committedPaint = context->committedPaint();
    const auto checkboxPaint = std::ranges::find_if(
        committedPaint.entries(),
        [checkbox](const UI::UICommittedPaintEntry& entry) { return entry.node == checkbox; });
    ASSERT_NE(checkboxPaint, committedPaint.entries().end());
    EXPECT_EQ(
        checkboxPaint->solidFill,
        (UI::UIPremultipliedRgba8Color{10, 20, 30, 255}));
}

TEST(UICheckboxTest, HoveredFocusedPressedAndDisabledStatesResolveCommittedOuterIndicatorPaint)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(checkbox, solidFill(10, 20, 30)));
    assertOk(updater.setCheckboxPaint(
        checkbox,
        {
            .checkedIndicatorColor = {},
            .checkedIndicatorInset = 6.0F,
            .hoveredIndicatorColor = {.red = 40, .green = 50, .blue = 60, .alpha = 255},
            .focusedIndicatorColor = {.red = 70, .green = 80, .blue = 90, .alpha = 255},
            .pressedIndicatorColor = {.red = 100, .green = 110, .blue = 120, .alpha = 255},
        }));

    publishLayout(*context);
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{10, 20, 30, 255}));

    assertOk(context->requestFocus(checkbox));
    publishLayout(*context);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{10, 20, 30, 255}));

    auto keyboardNavigation = context->routeFocusNavigation(
        UI::UIFocusNavigationDirection::Right, true,
        UI::UIInputModality::Keyboard);
    ASSERT_TRUE(keyboardNavigation.has_value()) << keyboardNavigation.error().message;
    publishLayout(*context);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{70, 80, 90, 255}));

    auto movedInside = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::Move,
        1,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(movedInside.has_value()) << (movedInside ? "" : movedInside.error().message);
    publishLayout(*context);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{40, 50, 60, 255}));

    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        2,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    publishLayout(*context);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{100, 110, 120, 255}));

    auto up = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        3,
        {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    publishLayout(*context);
    ASSERT_EQ(context->committedPaint().size(), 1U);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{40, 50, 60, 255}));

    auto movedOutside = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::Move,
        4,
        {.x = 80.0F, .y = 80.0F}));
    ASSERT_TRUE(movedOutside.has_value()) << (movedOutside ? "" : movedOutside.error().message);
    publishLayout(*context);
    EXPECT_EQ(context->defaultActionFocus(), checkbox);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{10, 20, 30, 255}));

    assertOk(updater.setEnabled(checkbox, false));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    publishLayout(*context);
    EXPECT_EQ(
        context->committedPaint().entries()[0].solidFill,
        (UI::UIPremultipliedRgba8Color{5, 11, 16, 140}));
}

TEST(UICheckboxTest, PrimaryClickTogglesAndFiresAction)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    int activations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{[&activations](const UI::UIButtonActionEvent&) noexcept {
            ++activations;
        }}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    publishLayout(*context);

    auto down = context->routePointerInput(
        makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto up = context->routePointerInput(
        makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);

    EXPECT_EQ(activations, 1);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_TRUE(*checked);

    auto down2 = context->routePointerInput(
        makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 3, {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(down2.has_value());
    auto up2 = context->routePointerInput(
        makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 4, {.x = 10.0F, .y = 10.0F}));
    ASSERT_TRUE(up2.has_value());
    EXPECT_EQ(activations, 2);
    checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_FALSE(*checked);
}

TEST(UICheckboxTest, CheckedIndicatorFollowsPointerStateAndSemantics)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(checkbox, solidFill(20, 40, 60, 128)));
    assertOk(updater.setCheckboxPaint(
        checkbox,
        {
            .checkedIndicatorColor = {
                .red = 200,
                .green = 100,
                .blue = 50,
                .alpha = 128,
            },
            .checkedIndicatorInset = 5.0F,
        }));
    int activations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{
            [&activations](const UI::UIButtonActionEvent&) noexcept {
                ++activations;
            }}));
    publishLayout(*context);

    const UI::UICommittedPaintView uncheckedPaint = context->committedPaint();
    ASSERT_EQ(uncheckedPaint.size(), 1U);
    EXPECT_EQ(uncheckedPaint.entries()[0].node, checkbox);
    const u64 uncheckedPaintRevision = uncheckedPaint.paintRevision();
    const UI::UISemanticsEntry* uncheckedSemantics =
        findSemanticsEntry(context->committedSemantics(), checkbox);
    ASSERT_NE(uncheckedSemantics, nullptr);
    EXPECT_FALSE(uncheckedSemantics->checked);

    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    auto up = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_EQ(activations, 1);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_TRUE(*checked);
    EXPECT_EQ(context->committedPaint().paintRevision(), uncheckedPaintRevision);
    ASSERT_EQ(context->committedPaint().size(), 1U);

    publishLayout(*context);
    const UI::UICommittedPaintView checkedPaint = context->committedPaint();
    ASSERT_EQ(checkedPaint.size(), 2U);
    EXPECT_EQ(checkedPaint.paintRevision(), uncheckedPaintRevision + 1U);
    const UI::UICommittedPaintEntry& track = checkedPaint.entries()[0];
    const UI::UICommittedPaintEntry& indicator = checkedPaint.entries()[1];
    EXPECT_EQ(track.node, checkbox);
    EXPECT_EQ(indicator.node, checkbox);
    EXPECT_EQ(track.paintOrdinal, 1U);
    EXPECT_EQ(indicator.paintOrdinal, 2U);
    EXPECT_EQ(
        indicator.worldRect,
        (UI::UILogicalRect{
            .x = 5.0F,
            .y = 5.0F,
            .width = 30.0F,
            .height = 30.0F,
        }));
    EXPECT_EQ(indicator.effectiveClip, track.effectiveClip);
    EXPECT_EQ(
        indicator.solidFill,
        (UI::UIPremultipliedRgba8Color{
            .red = 100,
            .green = 50,
            .blue = 25,
            .alpha = 128,
        }));
    const UI::UISemanticsEntry* checkedSemantics =
        findSemanticsEntry(context->committedSemantics(), checkbox);
    ASSERT_NE(checkedSemantics, nullptr);
    EXPECT_TRUE(checkedSemantics->checked);
    EXPECT_TRUE(checkedSemantics->focused);

    down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        3));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    up = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        4));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_EQ(activations, 2);
    publishLayout(*context);
    ASSERT_EQ(context->committedPaint().size(), 1U);
    const UI::UISemanticsEntry* toggledOffSemantics =
        findSemanticsEntry(context->committedSemantics(), checkbox);
    ASSERT_NE(toggledOffSemantics, nullptr);
    EXPECT_FALSE(toggledOffSemantics->checked);
}

TEST(UICheckboxTest, PrimaryUpOutsideDoesNotToggle)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    int activations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{[&activations](const UI::UIButtonActionEvent&) noexcept {
            ++activations;
        }}));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    publishLayout(*context);

    {
        auto down = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 10.0F, .y = 10.0F}));
        ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    }
    // Up far outside.
    {
        auto up = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, {.x = 90.0F, .y = 90.0F}));
        ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    }
    EXPECT_EQ(activations, 0);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_FALSE(*checked);
}

TEST(UICheckboxTest, SetCheckedSilentAndRejectsWrongKind)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    auto button = context->rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(checkbox.hasValue());
    ASSERT_TRUE(button.has_value());

    auto updater = createUpdater(*context, root);
    auto defaultPaint = updater.checkboxPaint(checkbox);
    ASSERT_TRUE(defaultPaint.has_value())
        << (defaultPaint ? "" : defaultPaint.error().message);
    EXPECT_EQ(*defaultPaint, UI::UICheckboxPaint{});
    const UI::UICheckboxPaint expectedPaint{
        .checkedIndicatorColor = {
            .red = 10,
            .green = 20,
            .blue = 30,
            .alpha = 255,
        },
        .checkedIndicatorInset = 4.0F,
    };
    assertOk(updater.setCheckboxPaint(checkbox, expectedPaint));
    auto paint = updater.checkboxPaint(checkbox);
    ASSERT_TRUE(paint.has_value()) << (paint ? "" : paint.error().message);
    EXPECT_EQ(*paint, expectedPaint);

    UI::UICheckboxPaint invalidPaint = expectedPaint;
    invalidPaint.checkedIndicatorInset = -1.0F;
    auto invalid = updater.setCheckboxPaint(checkbox, invalidPaint);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, UI::UIErrorCode::InvalidControlValue);
    invalidPaint.checkedIndicatorInset =
        (std::numeric_limits<float>::quiet_NaN)();
    invalid = updater.setCheckboxPaint(checkbox, invalidPaint);
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, UI::UIErrorCode::InvalidControlValue);
    paint = updater.checkboxPaint(checkbox);
    ASSERT_TRUE(paint.has_value());
    EXPECT_EQ(*paint, expectedPaint);

    int activations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{[&activations](const UI::UIButtonActionEvent&) noexcept {
            ++activations;
        }}));
    assertOk(updater.setChecked(checkbox, true));
    EXPECT_EQ(activations, 0);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_TRUE(*checked);

    EXPECT_FALSE(updater.setCheckboxPaint(*button, expectedPaint).has_value());
    EXPECT_FALSE(updater.checkboxPaint(*button).has_value());
    EXPECT_FALSE(updater.setChecked(*button, true).has_value());
    EXPECT_FALSE(updater.isChecked(*button).has_value());
}

TEST(UICheckboxTest, PaintCapacityFailurePreservesPublishedSnapshotsAtomically)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 2,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 1,
            .buttonActionCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setBoxPaint(checkbox, solidFill(1, 2, 3)));
    assertOk(updater.setCheckboxPaint(
        checkbox,
        {
            .checkedIndicatorColor = {
                .red = 4,
                .green = 5,
                .blue = 6,
                .alpha = 255,
            },
            .checkedIndicatorInset = 4.0F,
        }));
    publishLayout(*context, 40.0F, 40.0F);

    const UI::UICommittedPaintView oldPaint = context->committedPaint();
    const UI::UICommittedSemanticsView oldSemantics =
        context->committedSemantics();
    ASSERT_EQ(oldPaint.size(), 1U);
    ASSERT_EQ(oldSemantics.size(), 1U);
    const UI::UISemanticsEntry* oldCheckboxSemantics =
        findSemanticsEntry(oldSemantics, checkbox);
    ASSERT_NE(oldCheckboxSemantics, nullptr);
    EXPECT_FALSE(oldCheckboxSemantics->checked);
    const auto* const oldPaintData = oldPaint.entries().data();
    const auto* const oldSemanticsData = oldSemantics.entries().data();

    assertOk(updater.setChecked(checkbox, true));
    const Core::Status rejected =
        context->commitLayout({.width = 40.0F, .height = 40.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);

    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_TRUE(*checked);
    EXPECT_EQ(context->committedPaint().entries().data(), oldPaintData);
    EXPECT_EQ(context->committedPaint().paintRevision(), oldPaint.paintRevision());
    EXPECT_EQ(context->committedSemantics().entries().data(), oldSemanticsData);
    EXPECT_EQ(
        context->committedSemantics().semanticsRevision(),
        oldSemantics.semanticsRevision());
    const UI::UISemanticsEntry* stillUnchecked =
        findSemanticsEntry(context->committedSemantics(), checkbox);
    ASSERT_NE(stillUnchecked, nullptr);
    EXPECT_FALSE(stillUnchecked->checked);
    EXPECT_TRUE(context->statistics().paintDirty);
    EXPECT_TRUE(context->statistics().semanticsDirty);

    assertOk(updater.setChecked(checkbox, false));
    publishLayout(*context, 40.0F, 40.0F);
    ASSERT_EQ(context->committedPaint().size(), 1U);
    const UI::UISemanticsEntry* recoveredSemantics =
        findSemanticsEntry(context->committedSemantics(), checkbox);
    ASSERT_NE(recoveredSemantics, nullptr);
    EXPECT_FALSE(recoveredSemantics->checked);
}

TEST(UICheckboxTest, KeyboardAcceptTogglesDefaultFocusedCheckbox)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto checkbox = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 100.0F)));
    publishLayout(*context);

    // Arm via pointer to set default-action focus, then keyboard Accept.
    {
        auto down = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 10.0F, .y = 10.0F}));
        ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
        auto up = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 2, {.x = 10.0F, .y = 10.0F}));
        ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    }
    auto checkedAfterClick = updater.isChecked(checkbox);
    ASSERT_TRUE(checkedAfterClick.has_value());
    EXPECT_TRUE(*checkedAfterClick);

    auto activate = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{3}, 3, UI::UIButtonActivationSource::Keyboard);
    ASSERT_TRUE(activate.has_value()) << (activate ? "" : activate.error().message);
    EXPECT_TRUE(activate->consumed);
    auto checkedAfterKey = updater.isChecked(checkbox);
    ASSERT_TRUE(checkedAfterKey.has_value());
    EXPECT_FALSE(*checkedAfterKey);
}

TEST(UICheckboxTest, GamepadSouthDownTogglesAndUpOnlyReleasesPressedState)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto gamepads = GamepadPool::Create(1);
    ASSERT_TRUE(gamepads.has_value());
    const auto gamepad = *gamepads->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId checkbox =
        createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    usize activationCount = 0;
    UI::UIButtonActionEvent activation{};
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{
            [&activationCount, &activation](
                const UI::UIButtonActionEvent& event) noexcept {
                ++activationCount;
                activation = event;
            }}));
    publishLayout(*context);

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    EXPECT_TRUE(focus->consumed);
    EXPECT_TRUE(focus->moved);
    ASSERT_EQ(focus->focus, checkbox);
    EXPECT_EQ(context->defaultActionFocus(), checkbox);

    const Platform::DigitalControlIdentity south =
        Platform::GamepadButtonControlIdentity{
            .routedWindow = window,
            .gamepad = gamepad,
            .button = Platform::GamepadButton::South,
        };
    auto down = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{10},
        100,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_TRUE(down->activated);

    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value()) << (checked ? "" : checked.error().message);
    EXPECT_TRUE(*checked);
    auto pressed = updater.isCheckboxPressed(checkbox);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    EXPECT_TRUE(*pressed);
    ASSERT_EQ(activationCount, 1U);
    EXPECT_EQ(activation.buttonNode, checkbox);
    EXPECT_EQ(activation.source, UI::UIButtonActivationSource::Gamepad);
    EXPECT_EQ(activation.platformFrame, Platform::PlatformFrameId{10});
    EXPECT_EQ(activation.sourceSequence, 100U);

    auto up = context->routeDefaultActionRelease(
        Platform::PlatformFrameId{11},
        101,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_TRUE(up->consumed);
    EXPECT_FALSE(up->activated);
    pressed = updater.isCheckboxPressed(checkbox);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    EXPECT_FALSE(*pressed);
    checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value()) << (checked ? "" : checked.error().message);
    EXPECT_TRUE(*checked);
    EXPECT_EQ(activationCount, 1U);

    auto duplicateUp = context->routeDefaultActionRelease(
        Platform::PlatformFrameId{12},
        102,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(duplicateUp.has_value())
        << (duplicateUp ? "" : duplicateUp.error().message);
    EXPECT_FALSE(duplicateUp->consumed);
    EXPECT_FALSE(duplicateUp->activated);
    EXPECT_EQ(activationCount, 1U);
}

TEST(UICheckboxTest, DisabledCheckboxRejectsGamepadSouthWithoutSideEffects)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto gamepads = GamepadPool::Create(1);
    ASSERT_TRUE(gamepads.has_value());
    const auto gamepad = *gamepads->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId checkbox =
        createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(checkbox, fixedSize(40.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(
        root.rootNodeId(),
        fixedSize(100.0F, 100.0F)));
    usize activationCount = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{
            [&activationCount](const UI::UIButtonActionEvent&) noexcept {
                ++activationCount;
            }}));
    publishLayout(*context);

    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_EQ(focus->focus, checkbox);
    assertOk(updater.setEnabled(checkbox, false));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());

    const Platform::DigitalControlIdentity south =
        Platform::GamepadButtonControlIdentity{
            .routedWindow = window,
            .gamepad = gamepad,
            .button = Platform::GamepadButton::South,
        };
    auto down = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{20},
        200,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_FALSE(down->consumed);
    EXPECT_FALSE(down->activated);

    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value()) << (checked ? "" : checked.error().message);
    EXPECT_FALSE(*checked);
    auto pressed = updater.isCheckboxPressed(checkbox);
    ASSERT_TRUE(pressed.has_value()) << (pressed ? "" : pressed.error().message);
    EXPECT_FALSE(*pressed);
    EXPECT_EQ(activationCount, 0U);

    auto up = context->routeDefaultActionRelease(
        Platform::PlatformFrameId{21},
        201,
        UI::UIButtonActivationSource::Gamepad,
        south);
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    EXPECT_FALSE(up->consumed);
    EXPECT_FALSE(up->activated);
    EXPECT_EQ(activationCount, 0U);
}

TEST(UICheckboxTest, SetCheckedDirtyQueueFailurePreservesState)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 3,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId checkbox = createCheckbox(*context, root.rootNodeId());
    const UI::UINodeId blocker = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());
    ASSERT_TRUE(blocker.hasValue());

    auto updater = createUpdater(*context, root);
    publishLayout(*context);
    assertOk(updater.setChecked(blocker, true));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 1U);

    const Core::Status rejected = updater.setChecked(checkbox, true);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_FALSE(*checked);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 1U);
}

TEST(UICheckboxTest, PointerActivationDirtyQueueFailurePreservesStateAndAction)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 4,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId checkbox = createCheckbox(*context, root.rootNodeId());
    const UI::UINodeId intrinsicSpacer = createRadioButton(*context, checkbox);
    const UI::UINodeId blocker = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());
    ASSERT_TRUE(intrinsicSpacer.hasValue());
    ASSERT_TRUE(blocker.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setPointerHitPolicy(
        intrinsicSpacer,
        UI::UIPointerHitPolicy::Ignore));
    int activations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{[&activations](const UI::UIButtonActionEvent&) noexcept {
            ++activations;
        }}));
    publishLayout(*context);

    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 4.0F, .y = 4.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    ASSERT_TRUE(down->consumed);
    publishLayout(*context);

    assertOk(updater.setChecked(blocker, true));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 1U);
    auto up = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        {.x = 4.0F, .y = 4.0F}));
    ASSERT_FALSE(up.has_value());
    EXPECT_EQ(up.error().code, UI::UIErrorCode::CapacityExceeded);

    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_FALSE(*checked);
    EXPECT_EQ(activations, 0);
}

TEST(UICheckboxTest, KeyboardActivationDirtyQueueFailurePreservesStateAndAction)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 3,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 1,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId checkbox = createCheckbox(*context, root.rootNodeId());
    const UI::UINodeId blocker = createCheckbox(*context, root.rootNodeId());
    ASSERT_TRUE(checkbox.hasValue());
    ASSERT_TRUE(blocker.hasValue());

    auto updater = createUpdater(*context, root);
    int activations = 0;
    assertOk(updater.setCheckboxAction(
        checkbox,
        UI::UIButtonActionCallback{[&activations](const UI::UIButtonActionEvent&) noexcept {
            ++activations;
        }}));
    publishLayout(*context);
    auto focus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(focus.has_value()) << (focus ? "" : focus.error().message);
    ASSERT_TRUE(focus->moved);
    ASSERT_EQ(focus->focus, checkbox);
    publishLayout(*context);

    assertOk(updater.setChecked(blocker, true));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 1U);
    auto activate = context->routeDefaultActionActivate(
        Platform::PlatformFrameId{1},
        1,
        UI::UIButtonActivationSource::Keyboard);
    ASSERT_FALSE(activate.has_value());
    EXPECT_EQ(activate.error().code, UI::UIErrorCode::CapacityExceeded);

    auto checked = updater.isChecked(checkbox);
    ASSERT_TRUE(checked.has_value());
    EXPECT_FALSE(*checked);
    EXPECT_EQ(activations, 0);
}

TEST(UICheckboxTest, TypedWrappersRejectOffThreadBeforeKindResolution)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto updater = createUpdater(*context, root);
    auto buttonResult = updater.createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(buttonResult.has_value())
        << (buttonResult ? "" : buttonResult.error().message);
    const UI::UINodeId wrongKind = *buttonResult;

    std::array<bool, 5> succeeded{};
    std::array<Core::ErrorCode, 5> errors{};
    std::thread worker([&]() {
        auto setAction = updater.setCheckboxAction(
            wrongKind,
            UI::UIButtonActionCallback{
                [](const UI::UIButtonActionEvent&) noexcept {}});
        succeeded[0] = setAction.has_value();
        errors[0] = setAction ? Core::ErrorCode{} : setAction.error().code;

        auto clearAction = updater.clearCheckboxAction(wrongKind);
        succeeded[1] = clearAction.has_value();
        errors[1] = clearAction ? Core::ErrorCode{} : clearAction.error().code;

        auto setPaint = updater.setCheckboxPaint(wrongKind, {});
        succeeded[2] = setPaint.has_value();
        errors[2] = setPaint ? Core::ErrorCode{} : setPaint.error().code;

        auto paint = updater.checkboxPaint(wrongKind);
        succeeded[3] = paint.has_value();
        errors[3] = paint ? Core::ErrorCode{} : paint.error().code;

        auto pressed = updater.isCheckboxPressed(wrongKind);
        succeeded[4] = pressed.has_value();
        errors[4] = pressed ? Core::ErrorCode{} : pressed.error().code;
    });
    worker.join();

    for (usize index = 0; index < succeeded.size(); ++index) {
        EXPECT_FALSE(succeeded[index]);
        EXPECT_EQ(errors[index], UI::UIErrorCode::WrongOwnerThread);
    }
}

} // namespace
} // namespace Tina::Tests
