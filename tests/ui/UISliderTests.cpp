#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <cmath>
#include <memory>
#include <memory_resource>
#include <limits>
#include <type_traits>
#include <utility>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

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

[[nodiscard]] UI::UINodeId createSlider(UI::UIContext& context, UI::UINodeId parent)
{
    auto result = context.rootBuilder().createElement(parent, UI::makeSliderElement());
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

[[nodiscard]] UI::UIBoxPaint solidFill(u8 red, u8 green, u8 blue, u8 alpha = 255) noexcept
{
    UI::UIBoxPaint paint;
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

struct CallbackDestructionProbe final {
    bool* invocationActive = nullptr;
    bool* destroyedDuringInvocation = nullptr;
    bool ownsProbe = true;

    CallbackDestructionProbe(
        bool& active,
        bool& destroyed) noexcept
        : invocationActive(&active),
          destroyedDuringInvocation(&destroyed)
    {
    }

    CallbackDestructionProbe(const CallbackDestructionProbe&) = delete;
    CallbackDestructionProbe& operator=(const CallbackDestructionProbe&) = delete;

    CallbackDestructionProbe(CallbackDestructionProbe&& other) noexcept
        : invocationActive(other.invocationActive),
          destroyedDuringInvocation(other.destroyedDuringInvocation),
          ownsProbe(std::exchange(other.ownsProbe, false))
    {
    }

    CallbackDestructionProbe& operator=(CallbackDestructionProbe&&) = delete;

    ~CallbackDestructionProbe() noexcept
    {
        if (ownsProbe
            && invocationActive != nullptr
            && destroyedDuringInvocation != nullptr
            && *invocationActive) {
            *destroyedDuringInvocation = true;
        }
    }
};

static_assert(std::is_nothrow_move_constructible_v<CallbackDestructionProbe>);
static_assert(std::is_nothrow_destructible_v<CallbackDestructionProbe>);

TEST(UISliderTest, DefaultsAndSetValue)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());

    auto updater = createUpdater(*context, root);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value()) << (value ? "" : value.error().message);
    EXPECT_FLOAT_EQ(*value, 0.0F);

    assertOk(updater.setSliderRange(slider, 0.0F, 1.0F, 0.25F));
    assertOk(updater.setSliderValue(slider, 0.6F));
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.5F); // quantized to step 0.25 from min

    auto button = context->rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    EXPECT_FALSE(updater.setSliderValue(*button, 0.1F).has_value());
}

TEST(UISliderTest, PointerDragMapsXToValueAndFiresChange)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(200.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(slider, fixedSize(100.0F, 20.0F)));
    assertOk(updater.setSliderRange(slider, 0.0F, 100.0F, 1.0F));

    int changes = 0;
    float lastValue = -1.0F;
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{[&](const UI::UISliderChangeEvent& event) noexcept {
            ++changes;
            lastValue = event.value;
        }}));
    assertOk(context->commitLayout({.width = 200.0F, .height = 40.0F}));

    // Down at left edge -> ~0
    {
        auto down = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonDown, 1, {.x = 0.0F, .y = 10.0F}));
        ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
        EXPECT_TRUE(down->consumed);
    }
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.0F);

    // Drag to mid (~50)
    {
        auto move = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::Move, 2, {.x = 50.0F, .y = 10.0F}));
        ASSERT_TRUE(move.has_value()) << (move ? "" : move.error().message);
    }
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 50.0F, 1.0F);
    EXPECT_GE(changes, 1);
    EXPECT_NEAR(lastValue, *value, 1.0F);

    auto dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    EXPECT_TRUE(*dragging);

    {
        auto up = context->routePointerInput(
            makePointerInput(window, UI::UIRoutedPointerEventKind::ButtonUp, 3, {.x = 100.0F, .y = 10.0F}));
        ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    }
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 100.0F, 1.0F);
    dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    EXPECT_FALSE(*dragging);
}

TEST(UISliderTest, PointerMappingUsesThePaintTrackAndThumbCenter)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(slider, fixedSize(100.0F, 20.0F)));
    assertOk(updater.setBoxPaint(slider, solidFill(20, 30, 40)));
    assertOk(updater.setSliderPaint(
        slider,
        UI::UISliderPaint{
            .filledTrackColor = {.red = 40, .green = 160, .blue = 220, .alpha = 255},
            .thumbColor = {.red = 235, .green = 240, .blue = 245, .alpha = 255},
            .draggingThumbColor = {.red = 255, .green = 200, .blue = 40, .alpha = 255},
            .contentInset = 4.0F,
            .thumbExtent = 8.0F,
        }));
    assertOk(updater.setSliderRange(slider, 0.0F, 100.0F, 1.0F));
    assertOk(updater.setSliderValue(slider, 25.0F));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));

    // The committed thumb is x=23..31, so its center is x=27. Mapping must
    // use the same inset/center span and keep the value at 25.
    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 27.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 25.0F);

    auto up = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        {.x = 27.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 25.0F);
}

TEST(UISliderTest, PaintPublishesTrackFillThumbAndPressedResetState)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(slider, fixedSize(100.0F, 20.0F)));
    assertOk(updater.setBoxPaint(slider, solidFill(30, 40, 50)));
    const UI::UISliderPaint expectedPaint{
        .trackColor = {.red = 10, .green = 20, .blue = 30, .alpha = 255},
        .filledTrackColor = {.red = 40, .green = 160, .blue = 220, .alpha = 255},
        .thumbColor = {.red = 235, .green = 240, .blue = 245, .alpha = 255},
        .draggingThumbColor = {.red = 255, .green = 200, .blue = 40, .alpha = 255},
        .focusedThumbColor = {.red = 80, .green = 180, .blue = 250, .alpha = 255},
        .contentInset = 4.0F,
        .thumbExtent = 8.0F,
    };
    assertOk(updater.setSliderPaint(slider, expectedPaint));
    assertOk(updater.setSliderRange(slider, 0.0F, 100.0F, 1.0F));
    assertOk(updater.setSliderValue(slider, 25.0F));
    auto roundTrip = updater.sliderPaint(slider);
    ASSERT_TRUE(roundTrip.has_value()) << (roundTrip ? "" : roundTrip.error().message);
    EXPECT_EQ(*roundTrip, expectedPaint);
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));

    UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 4U);
    EXPECT_EQ(paint.entries()[0].worldRect, (UI::UILogicalRect{.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 20.0F}));
    EXPECT_EQ(paint.entries()[1].worldRect, (UI::UILogicalRect{.x = 4.0F, .y = 8.0F, .width = 92.0F, .height = 4.0F}));
    EXPECT_EQ(paint.entries()[2].worldRect, (UI::UILogicalRect{.x = 4.0F, .y = 8.0F, .width = 23.0F, .height = 4.0F}));
    EXPECT_EQ(paint.entries()[3].worldRect, (UI::UILogicalRect{.x = 23.0F, .y = 6.0F, .width = 8.0F, .height = 8.0F}));
    EXPECT_EQ(paint.entries()[0].paintOrdinal, 1U);
    EXPECT_EQ(paint.entries()[1].paintOrdinal, 2U);
    EXPECT_EQ(paint.entries()[2].paintOrdinal, 3U);
    EXPECT_EQ(paint.entries()[3].paintOrdinal, 4U);
    EXPECT_EQ(paint.entries()[1].solidFill, (UI::UIPremultipliedRgba8Color{.red = 10, .green = 20, .blue = 30, .alpha = 255}));
    EXPECT_EQ(paint.entries()[2].solidFill, (UI::UIPremultipliedRgba8Color{.red = 40, .green = 160, .blue = 220, .alpha = 255}));
    EXPECT_EQ(paint.entries()[3].solidFill, (UI::UIPremultipliedRgba8Color{.red = 235, .green = 240, .blue = 245, .alpha = 255}));

    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 75.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    EXPECT_TRUE(down->consumed);
    EXPECT_EQ(context->defaultActionFocus(), slider);
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 4U);
    EXPECT_NEAR(paint.entries()[3].worldRect.x, 70.84F, 0.001F);
    EXPECT_FLOAT_EQ(paint.entries()[3].worldRect.y, 6.0F);
    EXPECT_FLOAT_EQ(paint.entries()[3].worldRect.width, 8.0F);
    EXPECT_FLOAT_EQ(paint.entries()[3].worldRect.height, 8.0F);
    EXPECT_EQ(paint.entries()[3].solidFill, (UI::UIPremultipliedRgba8Color{.red = 255, .green = 200, .blue = 40, .alpha = 255}));
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 77.0F);
    auto dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    EXPECT_TRUE(*dragging);

    auto up = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        2,
        {.x = 75.0F, .y = 10.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 4U);
    EXPECT_EQ(
        paint.entries()[3].solidFill,
        (UI::UIPremultipliedRgba8Color{.red = 235, .green = 240, .blue = 245, .alpha = 255}));
    dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    EXPECT_FALSE(*dragging);

    assertOk(updater.setEnabled(slider, false));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 4U);
    EXPECT_EQ(paint.entries()[2].solidFill, (UI::UIPremultipliedRgba8Color{.red = 22, .green = 88, .blue = 121, .alpha = 140}));
    EXPECT_EQ(paint.entries()[3].solidFill, (UI::UIPremultipliedRgba8Color{.red = 129, .green = 132, .blue = 135, .alpha = 140}));
}

TEST(UISliderTest, ExplicitFocusPublishesFocusSemanticsAndFocusedThumb)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(slider, fixedSize(100.0F, 20.0F)));
    assertOk(updater.setBoxPaint(slider, solidFill(30, 40, 50)));
    assertOk(updater.setSliderPaint(
        slider,
        UI::UISliderPaint{
            .filledTrackColor = {.red = 40, .green = 160, .blue = 220, .alpha = 255},
            .thumbColor = {.red = 235, .green = 240, .blue = 245, .alpha = 255},
            .draggingThumbColor = {.red = 255, .green = 200, .blue = 40, .alpha = 255},
            .focusedThumbColor = {.red = 80, .green = 180, .blue = 250, .alpha = 255},
            .contentInset = 4.0F,
            .thumbExtent = 8.0F,
        }));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));

    auto tabFocus = context->routeDefaultActionFocusStep(false);
    ASSERT_TRUE(tabFocus.has_value()) << (tabFocus ? "" : tabFocus.error().message);
    EXPECT_TRUE(tabFocus->consumed);
    EXPECT_EQ(tabFocus->focus, slider);
    EXPECT_EQ(context->defaultActionFocus(), slider);
    assertOk(context->clearFocus());

    assertOk(context->requestFocus(slider));
    EXPECT_EQ(context->defaultActionFocus(), slider);
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));

    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 2U);
    EXPECT_EQ(
        paint.entries()[1].solidFill,
        (UI::UIPremultipliedRgba8Color{.red = 80, .green = 180, .blue = 250, .alpha = 255}));

    bool sawFocusedSlider = false;
    const UI::UICommittedSemanticsView semantics = context->committedSemantics();
    for (const UI::UISemanticsEntry& entry : semantics.entries())
    {
        if (entry.node == slider)
        {
            sawFocusedSlider = true;
            EXPECT_TRUE(entry.focused);
            EXPECT_TRUE(entry.hasRange);
        }
    }
    EXPECT_TRUE(sawFocusedSlider);
}

TEST(UISliderTest, HiddenAndDestroyClearFocusWithoutLeavingStaleState)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(slider, fixedSize(100.0F, 20.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    assertOk(context->requestFocus(slider));
    EXPECT_EQ(context->defaultActionFocus(), slider);

    UI::UILayoutStyle hidden = fixedSize(100.0F, 20.0F);
    hidden.visibility = UI::UIVisibility::Hidden;
    assertOk(updater.setLayoutStyle(slider, hidden));
    EXPECT_EQ(context->defaultActionFocus(), slider);
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());

    assertOk(updater.setLayoutStyle(slider, fixedSize(100.0F, 20.0F)));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    assertOk(context->requestFocus(slider));
    EXPECT_EQ(context->defaultActionFocus(), slider);

    assertOk(updater.destroy(slider));
    EXPECT_FALSE(context->contains(slider));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    EXPECT_FALSE(context->defaultActionFocus().hasValue());
}

TEST(UISliderTest, PaintCapacityFailurePreservesPublishedSnapshotAtomically)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 2,
            .rootCapacity = 1,
            .paintSnapshotCapacity = 2,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(slider, fixedSize(100.0F, 20.0F)));
    assertOk(updater.setBoxPaint(slider, solidFill(1, 2, 3)));
    assertOk(updater.setSliderPaint(
        slider,
        UI::UISliderPaint{
            .filledTrackColor = {.red = 4, .green = 5, .blue = 6, .alpha = 255},
            .thumbColor = {.red = 7, .green = 8, .blue = 9, .alpha = 255},
            .draggingThumbColor = {.red = 10, .green = 11, .blue = 12, .alpha = 255},
            .contentInset = 3.0F,
            .thumbExtent = 6.0F,
        }));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    const UI::UICommittedPaintView oldPaint = context->committedPaint();
    ASSERT_EQ(oldPaint.size(), 2U);
    const auto* const oldEntries = oldPaint.entries().data();
    const u64 oldRevision = oldPaint.paintRevision();

    assertOk(updater.setSliderValue(slider, 0.5F));
    const Core::Status rejected = context->commitLayout({.width = 100.0F, .height = 40.0F});
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    EXPECT_EQ(context->committedPaint().entries().data(), oldEntries);
    EXPECT_EQ(context->committedPaint().paintRevision(), oldRevision);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.5F);

    assertOk(updater.setSliderValue(slider, 0.0F));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    EXPECT_EQ(context->committedPaint().size(), 2U);
}

TEST(UISliderTest, PaintRejectsInvalidMetricsAndWrongKindsWithoutMutation)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto button = context->rootBuilder().createElement(root.rootNodeId(), UI::makeButtonElement());
    ASSERT_TRUE(button.has_value());
    auto updater = createUpdater(*context, root);
    const UI::UISliderPaint expected{
        .filledTrackColor = {.red = 10, .green = 20, .blue = 30, .alpha = 255},
        .thumbColor = {.red = 40, .green = 50, .blue = 60, .alpha = 255},
        .draggingThumbColor = {.red = 70, .green = 80, .blue = 90, .alpha = 255},
        .contentInset = 4.0F,
        .thumbExtent = 8.0F,
    };
    assertOk(updater.setSliderPaint(slider, expected));
    UI::UISliderPaint invalid = expected;
    invalid.contentInset = -1.0F;
    auto status = updater.setSliderPaint(slider, invalid);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidControlValue);
    invalid.contentInset = (std::numeric_limits<float>::quiet_NaN)();
    status = updater.setSliderPaint(slider, invalid);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, UI::UIErrorCode::InvalidControlValue);
    auto current = updater.sliderPaint(slider);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(*current, expected);
    EXPECT_FALSE(updater.setSliderPaint(*button, expected).has_value());
    EXPECT_FALSE(updater.sliderPaint(*button).has_value());
}

TEST(UISliderTest, CancelClearsDraggingWhenDirtyQueueIsFull)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(
        window,
        {
            .nodeCapacity = 8,
            .rootCapacity = 1,
            .dirtyQueueCapacity = 2,
            .paintSnapshotCapacity = 8,
        });
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(slider, fixedSize(100.0F, 20.0F)));
    assertOk(updater.setBoxPaint(slider, solidFill(20, 30, 40)));
    assertOk(updater.setSliderPaint(
        slider,
        UI::UISliderPaint{
            .filledTrackColor = {.red = 40, .green = 160, .blue = 220, .alpha = 255},
            .thumbColor = {.red = 235, .green = 240, .blue = 245, .alpha = 255},
            .draggingThumbColor = {.red = 255, .green = 200, .blue = 40, .alpha = 255},
            .contentInset = 4.0F,
            .thumbExtent = 8.0F,
        }));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));

    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 25.0F, .y = 10.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    auto dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    EXPECT_TRUE(*dragging);

    auto firstBlocker = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    auto secondBlocker = updater.createElement(root.rootNodeId(), UI::makePanelElement());
    ASSERT_TRUE(firstBlocker.has_value());
    ASSERT_TRUE(secondBlocker.has_value());
    assertOk(updater.setBoxPaint(*firstBlocker, solidFill(1, 2, 3)));
    assertOk(updater.setBoxPaint(*secondBlocker, solidFill(4, 5, 6)));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 2U);

    assertOk(context->cancelPointerInteraction(window));
    dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    EXPECT_FALSE(*dragging);
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
}

TEST(UISliderTest, ExtremeFiniteRangeAndOversizedInsetRemainStable)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(100.0F, 40.0F)));
    assertOk(updater.setLayoutStyle(slider, fixedSize(20.0F, 10.0F)));
    assertOk(updater.setBoxPaint(slider, solidFill(20, 30, 40)));
    assertOk(updater.setSliderPaint(
        slider,
        UI::UISliderPaint{
            .filledTrackColor = {.red = 40, .green = 160, .blue = 220, .alpha = 255},
            .thumbColor = {.red = 235, .green = 240, .blue = 245, .alpha = 255},
            .draggingThumbColor = {.red = 255, .green = 200, .blue = 40, .alpha = 255},
            .contentInset = 4.0F,
            .thumbExtent = 8.0F,
        }));
    const float maximum = (std::numeric_limits<float>::max)();
    assertOk(updater.setSliderRange(slider, -maximum, maximum, 0.0F));
    assertOk(updater.setSliderValue(slider, 0.0F));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));

    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 4.0F, .y = 5.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_TRUE(std::isfinite(*value));
    EXPECT_FLOAT_EQ(*value, -maximum);
    auto move = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::Move,
        2,
        {.x = 10.0F, .y = 5.0F}));
    ASSERT_TRUE(move.has_value()) << (move ? "" : move.error().message);
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_TRUE(std::isfinite(*value));
    EXPECT_FLOAT_EQ(*value, 0.0F);
    auto up = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonUp,
        3,
        {.x = 16.0F, .y = 5.0F}));
    ASSERT_TRUE(up.has_value()) << (up ? "" : up.error().message);
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_TRUE(std::isfinite(*value));
    EXPECT_FLOAT_EQ(*value, maximum);

    assertOk(updater.setSliderValue(slider, 0.0F));
    assertOk(updater.setSliderPaint(
        slider,
        UI::UISliderPaint{
            .filledTrackColor = {.red = 40, .green = 160, .blue = 220, .alpha = 255},
            .thumbColor = {.red = 235, .green = 240, .blue = 245, .alpha = 255},
            .draggingThumbColor = {.red = 255, .green = 200, .blue = 40, .alpha = 255},
            .contentInset = 100.0F,
            .thumbExtent = 8.0F,
        }));
    assertOk(context->commitLayout({.width = 100.0F, .height = 40.0F}));
    const UI::UICommittedPaintView paint = context->committedPaint();
    ASSERT_EQ(paint.size(), 2U);
    EXPECT_EQ(paint.entries()[1].worldRect, (UI::UILogicalRect{
        .x = 6.0F,
        .y = 1.0F,
        .width = 8.0F,
        .height = 8.0F,
    }));
    EXPECT_TRUE(std::isfinite(paint.entries()[1].worldRect.x));

    down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        4,
        {.x = 0.0F, .y = 5.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_TRUE(std::isfinite(*value));
}

TEST(UISliderTest, RangeClampInvokesCallbackWhenValueChanges)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);
    assertOk(updater.setSliderValue(slider, 0.8F));
    int changes = 0;
    float callbackValue = -1.0F;
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{[&](const UI::UISliderChangeEvent& event) noexcept {
            ++changes;
            callbackValue = event.value;
        }}));
    assertOk(updater.setSliderRange(slider, 0.0F, 0.5F, 0.0F));
    EXPECT_EQ(changes, 1);
    EXPECT_FLOAT_EQ(callbackValue, 0.5F);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.5F);
}

TEST(UISliderTest, RejectsInvalidRange)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    auto slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);
    EXPECT_FALSE(updater.setSliderRange(slider, 1.0F, 0.0F, 0.1F).has_value());
    EXPECT_FALSE(updater.setSliderRange(slider, 0.0F, 1.0F, -0.1F).has_value());
}

TEST(UISliderTest, SetValueDirtyQueueFailurePreservesStateAndSkipsCallback)
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
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    const UI::UINodeId blocker = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    ASSERT_TRUE(blocker.hasValue());

    auto updater = createUpdater(*context, root);
    int changes = 0;
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{[&changes](const UI::UISliderChangeEvent&) noexcept {
            ++changes;
        }}));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));
    assertOk(updater.setSliderValue(blocker, 0.5F));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 1U);

    const Core::Status rejected = updater.setSliderValue(slider, 0.75F);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::CapacityExceeded);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.0F);
    EXPECT_EQ(changes, 0);
    EXPECT_EQ(context->statistics().dirtyQueuePendingCount, 1U);
}

TEST(UISliderTest, PointerDirtyQueueFailurePreservesStateAndSkipsCallback)
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
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    const UI::UINodeId intrinsicSpacer = createRadioButton(*context, slider);
    const UI::UINodeId blocker = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    ASSERT_TRUE(intrinsicSpacer.hasValue());
    ASSERT_TRUE(blocker.hasValue());

    auto updater = createUpdater(*context, root);
    assertOk(updater.setPointerHitPolicy(
        intrinsicSpacer,
        UI::UIPointerHitPolicy::Ignore));
    int changes = 0;
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{[&changes](const UI::UISliderChangeEvent&) noexcept {
            ++changes;
        }}));
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto down = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::ButtonDown,
        1,
        {.x = 0.0F, .y = 4.0F}));
    ASSERT_TRUE(down.has_value()) << (down ? "" : down.error().message);
    assertOk(context->commitLayout({.width = 100.0F, .height = 100.0F}));

    auto dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    ASSERT_TRUE(*dragging);
    assertOk(updater.setSliderValue(blocker, 0.5F));
    ASSERT_EQ(context->statistics().dirtyQueuePendingCount, 1U);
    auto move = context->routePointerInput(makePointerInput(
        window,
        UI::UIRoutedPointerEventKind::Move,
        2,
        {.x = 12.0F, .y = 4.0F}));
    ASSERT_FALSE(move.has_value());
    EXPECT_EQ(move.error().code, UI::UIErrorCode::CapacityExceeded);

    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.0F);
    EXPECT_EQ(changes, 0);
    dragging = updater.isSliderDragging(slider);
    ASSERT_TRUE(dragging.has_value());
    EXPECT_TRUE(*dragging);
}

TEST(UISliderTest, CallbackRemainsRegisteredWhenItDoesNotMutate)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);

    int callbackCount = 0;
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{[&callbackCount](
            const UI::UISliderChangeEvent&) noexcept {
            ++callbackCount;
        }}));
    assertOk(updater.setSliderValue(slider, 0.25F));
    assertOk(updater.setSliderValue(slider, 0.5F));

    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.5F);
    EXPECT_EQ(callbackCount, 2);
}

TEST(UISliderTest, CallbackCanClearItselfWithoutEarlyDestruction)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);

    struct State final {
        UI::UITreeUpdater* updater = nullptr;
        UI::UINodeId slider{};
        int callbackCount = 0;
        bool clearSucceeded = false;
        bool invocationActive = false;
        bool destroyedDuringInvocation = false;
    } state{&updater, slider};

    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{
            [&state,
             probe = CallbackDestructionProbe{
                 state.invocationActive,
                 state.destroyedDuringInvocation}](
                const UI::UISliderChangeEvent&) mutable noexcept {
                state.invocationActive = true;
                ++state.callbackCount;
                state.clearSucceeded = state.updater->clearSliderChangeCallback(
                    state.slider).has_value();
                state.invocationActive = false;
            }}));

    assertOk(updater.setSliderValue(slider, 0.25F));
    EXPECT_EQ(state.callbackCount, 1);
    EXPECT_TRUE(state.clearSucceeded);
    EXPECT_FALSE(state.destroyedDuringInvocation);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.25F);

    assertOk(updater.setSliderValue(slider, 0.5F));
    EXPECT_EQ(state.callbackCount, 1);
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.5F);
}

TEST(UISliderTest, CallbackReplacementTakesEffectOnTheNextValueChange)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);

    struct State final {
        UI::UITreeUpdater* updater = nullptr;
        UI::UINodeId slider{};
        int oldCallbackCount = 0;
        int newCallbackCount = 0;
        bool replaceSucceeded = false;
        bool invocationActive = false;
        bool destroyedDuringInvocation = false;
    } state{&updater, slider};

    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{
            [&state,
             probe = CallbackDestructionProbe{
                 state.invocationActive,
                 state.destroyedDuringInvocation}](
                const UI::UISliderChangeEvent&) mutable noexcept {
                state.invocationActive = true;
                ++state.oldCallbackCount;
                state.replaceSucceeded = state.updater->setSliderChangeCallback(
                    state.slider,
                    UI::UISliderChangeCallback{
                        [&state](const UI::UISliderChangeEvent&) noexcept {
                            ++state.newCallbackCount;
                        }}).has_value();
                state.invocationActive = false;
            }}));

    assertOk(updater.setSliderValue(slider, 0.25F));
    EXPECT_EQ(state.oldCallbackCount, 1);
    EXPECT_EQ(state.newCallbackCount, 0);
    EXPECT_TRUE(state.replaceSucceeded);
    EXPECT_FALSE(state.destroyedDuringInvocation);

    assertOk(updater.setSliderValue(slider, 0.5F));
    EXPECT_EQ(state.oldCallbackCount, 1);
    EXPECT_EQ(state.newCallbackCount, 1);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.5F);
}

TEST(UISliderTest, DestroyingSliderFromCallbackDefersCallbackReclaim)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId slider = createSlider(*context, root.rootNodeId());
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);

    struct State final {
        UI::UITreeUpdater* updater = nullptr;
        UI::UINodeId slider{};
        int callbackCount = 0;
        bool destroySucceeded = false;
        bool invocationActive = false;
        bool destroyedDuringInvocation = false;
    } state{&updater, slider};
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{
            [&state,
             probe = CallbackDestructionProbe{
                 state.invocationActive,
                 state.destroyedDuringInvocation}](
                const UI::UISliderChangeEvent&) mutable noexcept {
                state.invocationActive = true;
                ++state.callbackCount;
                state.destroySucceeded = state.updater->destroy(state.slider).has_value();
                state.invocationActive = false;
            }}));

    assertOk(updater.setSliderValue(slider, 0.25F));
    EXPECT_EQ(state.callbackCount, 1);
    EXPECT_TRUE(state.destroySucceeded);
    EXPECT_FALSE(state.destroyedDuringInvocation);
    EXPECT_FALSE(context->contains(slider));
    EXPECT_FALSE(updater.sliderValue(slider).has_value());

    auto replacement = updater.createElement(root.rootNodeId(), UI::makeSliderElement());
    ASSERT_TRUE(replacement.has_value()) << replacement.error().message;
    EXPECT_NE(*replacement, slider);
    assertOk(updater.setSliderValue(*replacement, 0.75F));
    auto replacementValue = updater.sliderValue(*replacement);
    ASSERT_TRUE(replacementValue.has_value());
    EXPECT_FLOAT_EQ(*replacementValue, 0.75F);
}

TEST(UISliderTest, DestroyingRootFromCallbackDefersCallbackReclaim)
{
    auto windows = WindowPool::Create(1);
    ASSERT_TRUE(windows.has_value());
    const auto window = *windows->tryEmplace(1);
    auto context = createContext(window);
    ASSERT_NE(context, nullptr);
    auto root = createRoot(*context);
    ASSERT_TRUE(root.hasValue());
    const UI::UINodeId rootId = root.rootNodeId();
    const UI::UINodeId slider = createSlider(*context, rootId);
    ASSERT_TRUE(slider.hasValue());
    auto updater = createUpdater(*context, root);

    struct State final {
        UI::UIRootOwner* root = nullptr;
        int callbackCount = 0;
        bool invocationActive = false;
        bool destroyedDuringInvocation = false;
    } state{&root};
    assertOk(updater.setSliderChangeCallback(
        slider,
        UI::UISliderChangeCallback{
            [&state,
             probe = CallbackDestructionProbe{
                 state.invocationActive,
                 state.destroyedDuringInvocation}](
                const UI::UISliderChangeEvent&) mutable noexcept {
                state.invocationActive = true;
                ++state.callbackCount;
                state.root->reset();
                state.invocationActive = false;
            }}));

    assertOk(updater.setSliderValue(slider, 0.25F));
    EXPECT_EQ(state.callbackCount, 1);
    EXPECT_FALSE(state.destroyedDuringInvocation);
    EXPECT_FALSE(root.hasValue());
    EXPECT_FALSE(context->contains(rootId));
    EXPECT_FALSE(context->contains(slider));
    EXPECT_EQ(context->liveRootCount(), 0U);
    EXPECT_EQ(context->liveNodeCount(), 0U);

    auto replacementRoot = createRoot(*context);
    ASSERT_TRUE(replacementRoot.hasValue());
    auto replacementUpdater = createUpdater(*context, replacementRoot);
    auto replacementSlider = replacementUpdater.createElement(
        replacementRoot.rootNodeId(), UI::makeSliderElement());
    ASSERT_TRUE(replacementSlider.has_value());
    assertOk(replacementUpdater.setSliderValue(*replacementSlider, 0.5F));
}

} // namespace
} // namespace Tina::Tests
