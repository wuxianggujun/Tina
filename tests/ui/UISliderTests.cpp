#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>
#include <memory_resource>
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
    auto result = context.rootBuilder().createSlider(parent);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result ? *result : UI::UINodeId{};
}

[[nodiscard]] UI::UINodeId createRadioButton(UI::UIContext& context, UI::UINodeId parent)
{
    auto result = context.rootBuilder().createRadioButton(parent);
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

    auto button = context->rootBuilder().createButton(root.rootNodeId());
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

} // namespace
} // namespace Tina::Tests
