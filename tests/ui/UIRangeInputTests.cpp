#include <gtest/gtest.h>

#include <tina/core/id/GenerationPool.hpp>
#include <tina/ui/UI.hpp>

#include <memory>

namespace Tina::Tests {
namespace {

using WindowPool = Core::GenerationPool<int, Platform::WindowRegistryTag>;

[[nodiscard]] UI::UILayoutStyle fixedSize(float width, float height) noexcept
{
    UI::UILayoutStyle style;
    style.size.width = UI::UILayoutLength::Px(width);
    style.size.height = UI::UILayoutLength::Px(height);
    return style;
}

void expectOk(Core::Status status)
{
    EXPECT_TRUE(status.has_value()) << (status ? "" : status.error().message);
}

class UIRangeInputTest : public testing::Test {
  protected:
    void SetUp() override
    {
        auto windowsResult = WindowPool::Create(1);
        ASSERT_TRUE(windowsResult.has_value());
        windows = std::make_unique<WindowPool>(std::move(*windowsResult));
        auto windowResult = windows->tryEmplace(1);
        ASSERT_TRUE(windowResult.has_value());
        window = *windowResult;

        auto contextResult = UI::UIContext::Create(window, {
                                                               .nodeCapacity = 16,
                                                               .rootCapacity = 1,
                                                               .dirtyQueueCapacity = 16,
                                                               .paintSnapshotCapacity = 16,
                                                               .routePathCapacity = 8,
                                                               .buttonActionCapacity = 8,
                                                           });
        ASSERT_TRUE(contextResult.has_value()) << (contextResult ? "" : contextResult.error().message);
        context = std::move(*contextResult);
        auto rootResult = context->rootBuilder().createRoot();
        ASSERT_TRUE(rootResult.has_value());
        root = std::move(*rootResult);
        auto updaterResult = context->treeUpdater(root);
        ASSERT_TRUE(updaterResult.has_value());
        updater = std::move(*updaterResult);
        expectOk(updater.setLayoutStyle(root.rootNodeId(), fixedSize(160.0F, 120.0F)));
    }

    [[nodiscard]] UI::UINodeId addSlider(bool readOnly = false)
    {
        UI::UIElementDescriptor descriptor = UI::makeSliderElement();
        descriptor.semantics.readOnly = readOnly;
        auto slider = updater.createElement(root.rootNodeId(), descriptor);
        EXPECT_TRUE(slider.has_value()) << (slider ? "" : slider.error().message);
        if (!slider)
        {
            return {};
        }
        expectOk(updater.setLayoutStyle(*slider, fixedSize(120.0F, 24.0F)));
        return *slider;
    }

    [[nodiscard]] UI::UINodeId addButton()
    {
        auto button = updater.createElement(root.rootNodeId(), UI::makeButtonElement("Other"));
        EXPECT_TRUE(button.has_value()) << (button ? "" : button.error().message);
        if (!button)
        {
            return {};
        }
        expectOk(updater.setLayoutStyle(*button, fixedSize(80.0F, 24.0F)));
        return *button;
    }

    void commit()
    {
        expectOk(context->commitLayout({.width = 160.0F, .height = 120.0F}));
    }

    [[nodiscard]] Platform::DigitalControlIdentity key(Platform::Key value) const noexcept
    {
        return Platform::KeyControlIdentity{.window = window, .key = value};
    }

    [[nodiscard]] UI::UIPointerInputEvent pointerDown(u64 sequence, UI::UILogicalPoint position) const noexcept
    {
        return {
            .platformFrame = Platform::PlatformFrameId{sequence},
            .transitionOrdinal = static_cast<usize>(sequence - 1),
            .sourceSequence = sequence,
            .window = window,
            .pointer = Platform::PrimaryPointerId,
            .kind = UI::UIRoutedPointerEventKind::ButtonDown,
            .position = position,
            .button = Platform::PointerButton::Primary,
        };
    }

    std::unique_ptr<WindowPool> windows;
    Platform::WindowId window{};
    std::unique_ptr<UI::UIContext> context;
    UI::UIRootOwner root;
    UI::UITreeUpdater updater;
};

TEST_F(UIRangeInputTest, StepAdjustmentUsesCallbackPathAndExactControlLatch)
{
    const UI::UINodeId slider = addSlider();
    const UI::UINodeId other = addButton();
    ASSERT_TRUE(slider.hasValue() && other.hasValue());
    expectOk(updater.setSliderRange(slider, 0.0F, 10.0F, 2.0F));
    expectOk(updater.setSliderValue(slider, 6.0F));

    usize callbackCount = 0;
    UI::UISliderChangeEvent lastEvent{};
    expectOk(updater.setSliderChangeCallback(
        slider, UI::UISliderChangeCallback{[&](const UI::UISliderChangeEvent& event) noexcept {
            ++callbackCount;
            lastEvent = event;
        }}));
    commit();
    expectOk(context->requestFocus(slider));

    auto leftDown = context->routeRangeInputCommand({7}, 11, UI::UIRangeInputCommand::Decrease, true,
                                                    key(Platform::Key::Left));
    ASSERT_TRUE(leftDown.has_value()) << (leftDown ? "" : leftDown.error().message);
    EXPECT_TRUE(leftDown->consumed);
    EXPECT_TRUE(leftDown->changed);
    EXPECT_TRUE(leftDown->targeted);
    EXPECT_EQ(callbackCount, 1U);
    EXPECT_FLOAT_EQ(lastEvent.value, 4.0F);
    EXPECT_EQ(lastEvent.platformFrame, Platform::PlatformFrameId{7});
    EXPECT_EQ(lastEvent.sourceSequence, 11U);

    auto downDown = context->routeRangeInputCommand({8}, 12, UI::UIRangeInputCommand::Decrease, true,
                                                    key(Platform::Key::Down));
    ASSERT_TRUE(downDown.has_value()) << (downDown ? "" : downDown.error().message);
    EXPECT_TRUE(downDown->consumed);
    EXPECT_TRUE(downDown->changed);
    EXPECT_EQ(callbackCount, 2U);

    expectOk(context->requestFocus(other));
    auto downUp = context->routeRangeInputCommand({9}, 13, UI::UIRangeInputCommand::Decrease, false,
                                                  key(Platform::Key::Down));
    auto leftUp = context->routeRangeInputCommand({10}, 14, UI::UIRangeInputCommand::Decrease, false,
                                                  key(Platform::Key::Left));
    ASSERT_TRUE(downUp.has_value() && leftUp.has_value());
    EXPECT_TRUE(downUp->consumed);
    EXPECT_TRUE(leftUp->consumed);
    EXPECT_EQ(callbackCount, 2U);
}

TEST_F(UIRangeInputTest, DeclinesNoFocusIncompatibleReadOnlyDisabledAndClampedInput)
{
    const UI::UINodeId slider = addSlider();
    const UI::UINodeId readOnlySlider = addSlider(true);
    const UI::UINodeId button = addButton();
    ASSERT_TRUE(slider.hasValue() && readOnlySlider.hasValue() && button.hasValue());
    expectOk(updater.setSliderRange(slider, 0.0F, 1.0F, 0.25F));
    expectOk(updater.setSliderRange(readOnlySlider, 0.0F, 1.0F, 0.25F));
    commit();

    const auto routeDecrease = [&](u64 sequence, bool pressed = true) {
        return context->routeRangeInputCommand({20 + sequence}, sequence, UI::UIRangeInputCommand::Decrease,
                                               pressed, key(Platform::Key::Left));
    };
    auto noFocus = routeDecrease(1);
    ASSERT_TRUE(noFocus.has_value());
    EXPECT_FALSE(noFocus->consumed);

    expectOk(context->requestFocus(button));
    auto incompatible = routeDecrease(2);
    ASSERT_TRUE(incompatible.has_value());
    EXPECT_FALSE(incompatible->consumed);

    expectOk(context->requestFocus(readOnlySlider));
    auto readOnly = routeDecrease(3);
    ASSERT_TRUE(readOnly.has_value());
    EXPECT_FALSE(readOnly->consumed);
    EXPECT_TRUE(readOnly->targeted);
    auto pointer = context->routePointerInput(pointerDown(30, {.x = 110.0F, .y = 36.0F}));
    ASSERT_TRUE(pointer.has_value()) << (pointer ? "" : pointer.error().message);
    auto readOnlyValue = updater.sliderValue(readOnlySlider);
    ASSERT_TRUE(readOnlyValue.has_value());
    EXPECT_FLOAT_EQ(*readOnlyValue, 0.0F);

    expectOk(context->requestFocus(slider));
    auto clamped = routeDecrease(4);
    ASSERT_TRUE(clamped.has_value());
    EXPECT_FALSE(clamped->consumed);
    EXPECT_TRUE(clamped->targeted);
    auto clampedUp = routeDecrease(5, false);
    ASSERT_TRUE(clampedUp.has_value());
    EXPECT_FALSE(clampedUp->consumed);

    expectOk(updater.setSliderValue(slider, 0.5F));
    expectOk(context->requestFocus(slider));
    expectOk(updater.setEnabled(slider, false));
    auto disabled = routeDecrease(6);
    ASSERT_TRUE(disabled.has_value());
    EXPECT_FALSE(disabled->consumed);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.5F);
}

TEST_F(UIRangeInputTest, StepZeroUsesRangePercentAndAccessibilitySharesQuantizedMutation)
{
    const UI::UINodeId slider = addSlider();
    ASSERT_TRUE(slider.hasValue());
    expectOk(updater.setSliderRange(slider, 0.0F, 1.0F, 0.0F));
    expectOk(updater.setSliderValue(slider, 0.5F));
    usize callbackCount = 0;
    expectOk(updater.setSliderChangeCallback(
        slider, UI::UISliderChangeCallback{[&](const UI::UISliderChangeEvent&) noexcept { ++callbackCount; }}));
    commit();
    expectOk(context->requestFocus(slider));

    auto increased = context->routeRangeInputCommand({40}, 1, UI::UIRangeInputCommand::Increase, true,
                                                     key(Platform::Key::Right));
    ASSERT_TRUE(increased.has_value());
    EXPECT_TRUE(increased->changed);
    auto value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.51F);
    EXPECT_EQ(callbackCount, 1U);

    expectOk(updater.setSliderRange(slider, 0.0F, 1.0F, 0.25F));
    expectOk(context->performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::SetRangeValue,
        .node = slider,
        .rangeValue = 0.61,
    }));
    value = updater.sliderValue(slider);
    ASSERT_TRUE(value.has_value());
    EXPECT_FLOAT_EQ(*value, 0.5F);
    EXPECT_EQ(callbackCount, 2U);

    const UI::UINodeId readOnlySlider = addSlider(true);
    ASSERT_TRUE(readOnlySlider.hasValue());
    commit();
    auto rejected = context->performAccessibilityAction({
        .kind = UI::UIAccessibilityActionKind::SetRangeValue,
        .node = readOnlySlider,
        .rangeValue = 0.5,
    });
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, UI::UIErrorCode::InvalidAccessibilityAction);
}

} // namespace
} // namespace Tina::Tests
