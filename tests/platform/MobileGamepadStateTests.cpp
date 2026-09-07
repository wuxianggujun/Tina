#include "../../src/platform/MobileGamepadState.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <string>

namespace Tina::Platform {
namespace {

using Detail::MobileGamepadState;

MobileGamepadEvent connected(u64 device = 1)
{
    return {.kind = MobileGamepadEventKind::Connected, .deviceId = device,
            .device = {.name = Detail::makeMobileGamepadName("Test controller")}};
}

MobileGamepadEvent button(GamepadButton value, DigitalTransition state = DigitalTransition::Down, u64 device = 1)
{
    return {.kind = MobileGamepadEventKind::Button, .deviceId = device, .button = value, .state = state};
}

MobileGamepadEvent axis(GamepadAxis value, float position, u64 device = 1)
{
    return {.kind = MobileGamepadEventKind::Axis, .deviceId = device, .axis = value, .value = position};
}

class MobileGamepadStateTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto pool = Core::GenerationPool<int, WindowRegistryTag>::Create(1);
        ASSERT_TRUE(pool);
        windows.emplace(std::move(*pool));
        auto id = windows->tryEmplace(0);
        ASSERT_TRUE(id);
        window = *id;
        auto created = MobileGamepadState::Create();
        ASSERT_TRUE(created);
        state.emplace(std::move(*created));
        configure();
    }

    void configure(PlatformFrameCapacityConfig capacity = {})
    {
        auto created = PlatformFrameBuilder::Create(capacity);
        ASSERT_TRUE(created);
        frame.emplace(std::move(*created));
    }

    Core::Result<PlatformFrameView> poll(bool inputEnabled = true)
    {
        if (auto status = frame->beginFrame({frameId++}); !status) { return std::unexpected(status.error()); }
        WindowMetricsSnapshot metrics{};
        metrics.window = window;
        metrics.logicalExtent = {640, 480};
        metrics.framebufferExtent = {640, 480};
        metrics.contentScale = {1.0F, 1.0F};
        metrics.revision = 1;
        WindowInputSnapshot input{};
        input.window = window;
        input.sourceMetricsRevision = 1;
        EXPECT_TRUE(frame->setPrimaryWindowSnapshot(metrics, input));
        if (auto status = state->drain(queue, *frame, window, inputEnabled); !status) {
            (void)frame->discardFrame();
            return std::unexpected(status.error());
        }
        EXPECT_TRUE(frame->setGamepadSnapshots(state->snapshots()));
        return frame->finishFrame();
    }

    std::optional<Core::GenerationPool<int, WindowRegistryTag>> windows;
    std::optional<MobileGamepadState> state;
    std::optional<PlatformFrameBuilder> frame;
    MobileGamepadEventQueue queue;
    WindowId window{};
    u64 frameId = 1;
};

TEST(MobileGamepadQueueTest, FullQueuePreservesFifoAndCountsDrops)
{
    MobileGamepadEventQueue queue;
    for (usize index = 0; index < MobileGamepadEventCapacity; ++index) { ASSERT_TRUE(queue.tryPush(connected(index))); }
    EXPECT_FALSE(queue.tryPush(connected(999)));
    EXPECT_EQ(queue.droppedEventCount(), 1U);
    MobileGamepadEvent event{};
    for (usize index = 0; index < MobileGamepadEventCapacity; ++index) {
        ASSERT_TRUE(queue.tryPop(event));
        EXPECT_EQ(event.deviceId, index);
    }
    EXPECT_FALSE(queue.tryPop(event));
    ASSERT_TRUE(queue.tryPush(connected(77)));
    ASSERT_TRUE(queue.tryPop(event));
    EXPECT_EQ(event.deviceId, 77U);
    queue.requestResync();
    EXPECT_TRUE(queue.takeResyncRequest());
    EXPECT_FALSE(queue.takeResyncRequest());
}

TEST(MobileGamepadTranslationTest, MapsAndroidButtonsAxesAndHats)
{
    EXPECT_EQ(Detail::mapAndroidGamepadButton(96), GamepadButton::South);
    EXPECT_EQ(Detail::mapAndroidGamepadButton(108), GamepadButton::Start);
    EXPECT_EQ(Detail::mapAndroidGamepadButton(19), GamepadButton::DpadUp);
    EXPECT_FALSE(Detail::mapAndroidGamepadButton(104));
    EXPECT_FALSE(Detail::mapAndroidGamepadButton(-1));
    EXPECT_EQ(Detail::mapAndroidGamepadAxis(14), GamepadAxis::RightY);
    EXPECT_EQ(Detail::mapAndroidGamepadAxis(17), GamepadAxis::LeftTrigger);
    EXPECT_FALSE(Detail::mapAndroidGamepadAxis(15));
    EXPECT_TRUE(Detail::isAndroidGamepadHatX(15));
    EXPECT_TRUE(Detail::isAndroidGamepadHatY(16));
}

TEST(MobileGamepadTranslationTest, FiltersSticksButPublishesSignedTriggerRange)
{
    EXPECT_FLOAT_EQ(Detail::filterMobileGamepadAxis(GamepadAxis::LeftX, 0.18F), 0.0F);
    EXPECT_FLOAT_EQ(Detail::filterMobileGamepadAxis(GamepadAxis::LeftY, -1.0F), -1.0F);
    EXPECT_FLOAT_EQ(Detail::filterMobileGamepadAxis(GamepadAxis::LeftTrigger, 0.0F), -1.0F);
    EXPECT_FLOAT_EQ(Detail::filterMobileGamepadAxis(GamepadAxis::LeftTrigger, 0.5F), 0.0F);
    EXPECT_FLOAT_EQ(Detail::filterMobileGamepadAxis(GamepadAxis::RightTrigger, 1.0F), 1.0F);
    EXPECT_FALSE(Detail::mobileGamepadAxisChanged(0.5F, 0.51F));
    EXPECT_TRUE(Detail::mobileGamepadAxisChanged(-0.999F, -1.0F));
}

TEST(MobileGamepadTranslationTest, IdentityTruncationPreservesUtf8)
{
    const std::string label = std::string(62, 'x') + "\xF0\x9F\x8E\xAE";
    EXPECT_EQ(Detail::makeMobileGamepadName(label).length, 62U);
    EXPECT_TRUE(Detail::makeMobileGamepadName("\xFF").view().empty());
    EXPECT_EQ(Detail::classifyMobileGamepadLayout("DualSense Wireless Controller"), GamepadLayout::PlayStation);
    EXPECT_EQ(Detail::classifyMobileGamepadLayout("Xbox Wireless Controller"), GamepadLayout::Xbox);
}

TEST_F(MobileGamepadStateTest, ConnectionSnapshotsDeduplicateButtonsAndStableRevisions)
{
    ASSERT_TRUE(queue.tryPush(connected()));
    ASSERT_TRUE(queue.tryPush(button(GamepadButton::South)));
    ASSERT_TRUE(queue.tryPush(button(GamepadButton::South)));
    auto first = poll();
    ASSERT_TRUE(first);
    ASSERT_EQ(first->gamepads().size(), 1U);
    ASSERT_EQ(first->inputTransitions().size(), 1U);
    EXPECT_TRUE(first->gamepads()[0].isHeld(GamepadButton::South));
    EXPECT_FLOAT_EQ(first->gamepads()[0].axis(GamepadAxis::LeftTrigger), -1.0F);
    const auto revision = first->gamepads()[0].revision;
    auto second = poll();
    ASSERT_TRUE(second);
    EXPECT_TRUE(second->inputTransitions().empty());
    EXPECT_EQ(second->gamepads()[0].revision, revision);
}

TEST_F(MobileGamepadStateTest, HatAndKeySourcesDoNotReleaseEachOther)
{
    ASSERT_TRUE(queue.tryPush(connected()));
    ASSERT_TRUE(queue.tryPush({.kind = MobileGamepadEventKind::HatX, .deviceId = 1, .value = -1.0F}));
    ASSERT_TRUE(queue.tryPush(button(GamepadButton::DpadLeft)));
    ASSERT_TRUE(queue.tryPush({.kind = MobileGamepadEventKind::HatX, .deviceId = 1, .value = 0.0F}));
    auto held = poll();
    ASSERT_TRUE(held);
    EXPECT_TRUE(held->gamepads()[0].isHeld(GamepadButton::DpadLeft));
    EXPECT_EQ(held->inputTransitions().size(), 1U);
    ASSERT_TRUE(queue.tryPush(button(GamepadButton::DpadLeft, DigitalTransition::Up)));
    auto released = poll();
    ASSERT_TRUE(released);
    EXPECT_FALSE(released->gamepads()[0].isHeld(GamepadButton::DpadLeft));
    EXPECT_EQ(released->inputTransitions().size(), 1U);
}

TEST_F(MobileGamepadStateTest, DisconnectCancelsBeforeEventAndReusesOnlyANewGeneration)
{
    ASSERT_TRUE(queue.tryPush(connected()));
    ASSERT_TRUE(queue.tryPush(button(GamepadButton::South)));
    auto first = poll();
    ASSERT_TRUE(first);
    const GamepadId old = first->gamepads()[0].gamepad;
    ASSERT_TRUE(queue.tryPush({.kind = MobileGamepadEventKind::Disconnected, .deviceId = 1}));
    ASSERT_TRUE(queue.tryPush(connected()));
    auto next = poll();
    ASSERT_TRUE(next);
    ASSERT_EQ(next->inputTransitions().size(), 1U);
    ASSERT_EQ(next->platformEvents().size(), 2U);
    const auto* cancel = std::get_if<InputCancelTransition>(&next->inputTransitions()[0].payload);
    ASSERT_NE(cancel, nullptr);
    EXPECT_EQ(cancel->gamepad, old);
    EXPECT_LT(next->inputTransitions()[0].sequence, next->platformEvents()[0].sequence);
    EXPECT_EQ(next->gamepads()[0].gamepad.index(), old.index());
    EXPECT_NE(next->gamepads()[0].gamepad, old);
    EXPECT_FALSE(next->gamepads()[0].isHeld(GamepadButton::South));
}

TEST_F(MobileGamepadStateTest, QueueLossResetsBothStreamsAndRequestsLiveEnumeration)
{
    ASSERT_TRUE(queue.tryPush(connected()));
    auto first = poll();
    ASSERT_TRUE(first);
    const GamepadId old = first->gamepads()[0].gamepad;
    for (usize index = 0; index < MobileGamepadEventCapacity; ++index) { ASSERT_TRUE(queue.tryPush(connected())); }
    EXPECT_FALSE(queue.tryPush({.kind = MobileGamepadEventKind::Disconnected, .deviceId = 1}));
    auto reset = poll();
    ASSERT_TRUE(reset);
    EXPECT_TRUE(reset->gamepads().empty());
    EXPECT_TRUE(std::holds_alternative<InputStreamReset>(reset->inputTransitions().back().payload));
    EXPECT_TRUE(std::holds_alternative<PlatformEventStreamReset>(reset->platformEvents().back().payload));
    EXPECT_TRUE(queue.takeResyncRequest());
    ASSERT_TRUE(queue.tryPush(connected()));
    auto rebuilt = poll();
    ASSERT_TRUE(rebuilt);
    EXPECT_NE(rebuilt->gamepads()[0].gamepad, old);
}

TEST_F(MobileGamepadStateTest, SmallFramesDeferConnectionsWithoutAResetLoop)
{
    configure({.inputTransitionCapacity = 1, .platformEventCapacity = 1});
    ASSERT_TRUE(queue.tryPush(connected()));
    ASSERT_TRUE(queue.tryPush(connected(2)));
    auto first = poll();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->gamepads().size(), 1U);
    EXPECT_FALSE(queue.takeResyncRequest());
    auto second = poll();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->gamepads().size(), 2U);
    EXPECT_EQ(second->platformEvents().size(), 1U);
}

TEST_F(MobileGamepadStateTest, OneTransitionFramesReleaseBeforePressingAReversedHat)
{
    configure({.inputTransitionCapacity = 1});
    ASSERT_TRUE(queue.tryPush(connected()));
    ASSERT_TRUE(queue.tryPush({.kind = MobileGamepadEventKind::HatX, .deviceId = 1, .value = -1.0F}));
    ASSERT_TRUE(poll());
    ASSERT_TRUE(queue.tryPush({.kind = MobileGamepadEventKind::HatX, .deviceId = 1, .value = 1.0F}));
    auto released = poll();
    ASSERT_TRUE(released);
    EXPECT_FALSE(released->gamepads()[0].isHeld(GamepadButton::DpadLeft));
    EXPECT_FALSE(released->gamepads()[0].isHeld(GamepadButton::DpadRight));
    auto pressed = poll();
    ASSERT_TRUE(pressed);
    EXPECT_TRUE(pressed->gamepads()[0].isHeld(GamepadButton::DpadRight));
    EXPECT_FALSE(queue.takeResyncRequest());
}

TEST_F(MobileGamepadStateTest, SixteenFullyActiveDevicesConvergeAcrossDefaultFrames)
{
    for (u64 device = 1; device <= PlatformFrameBuilder::MaximumGamepadSlots; ++device) {
        ASSERT_TRUE(queue.tryPush(connected(device)));
        for (usize index = 0; index < GamepadButtonCount; ++index) {
            ASSERT_TRUE(queue.tryPush(button(static_cast<GamepadButton>(index), DigitalTransition::Down, device)));
        }
        for (usize index = 0; index < GamepadAxisCount; ++index) {
            ASSERT_TRUE(queue.tryPush(axis(static_cast<GamepadAxis>(index), 1.0F, device)));
        }
    }
    auto first = poll();
    ASSERT_TRUE(first);
    EXPECT_LE(first->inputTransitions().size(), PlatformFrameCapacityConfig::DefaultInputTransitionCapacity);
    auto second = poll();
    ASSERT_TRUE(second);
    ASSERT_EQ(second->gamepads().size(), PlatformFrameBuilder::MaximumGamepadSlots);
    for (const auto& gamepad : second->gamepads()) { EXPECT_TRUE(gamepad.heldButtons.all()); }
    EXPECT_FALSE(queue.takeResyncRequest());
    MobileGamepadEvent remaining{};
    EXPECT_FALSE(queue.tryPeek(remaining));
}

TEST_F(MobileGamepadStateTest, ChangedIdentityCanRetireAndReconnectAcrossSmallEventFrames)
{
    configure({.platformEventCapacity = 1});
    ASSERT_TRUE(queue.tryPush(connected()));
    auto first = poll();
    ASSERT_TRUE(first);
    const auto old = first->gamepads()[0].gamepad;
    auto replacement = connected();
    replacement.device.name = Detail::makeMobileGamepadName("Replacement");
    ASSERT_TRUE(queue.tryPush(replacement));
    auto retired = poll();
    ASSERT_TRUE(retired);
    EXPECT_TRUE(retired->gamepads().empty());
    auto reconnected = poll();
    ASSERT_TRUE(reconnected);
    EXPECT_NE(reconnected->gamepads()[0].gamepad, old);
    EXPECT_FALSE(queue.takeResyncRequest());
}

TEST_F(MobileGamepadStateTest, SuspendedInputStillProcessesDeviceLifecycle)
{
    ASSERT_TRUE(queue.tryPush(connected()));
    ASSERT_TRUE(queue.tryPush(button(GamepadButton::South)));
    auto suspended = poll(false);
    ASSERT_TRUE(suspended);
    EXPECT_FALSE(suspended->gamepads()[0].isHeld(GamepadButton::South));
    EXPECT_TRUE(suspended->inputTransitions().empty());
    ASSERT_TRUE(queue.tryPush({.kind = MobileGamepadEventKind::Disconnected, .deviceId = 1}));
    auto disconnected = poll(false);
    ASSERT_TRUE(disconnected);
    EXPECT_TRUE(disconnected->gamepads().empty());
}

TEST_F(MobileGamepadStateTest, UnknownDeviceIsIgnoredAndInvalidAxisFailsWithoutMutation)
{
    ASSERT_TRUE(queue.tryPush(button(GamepadButton::South, DigitalTransition::Down, 99)));
    ASSERT_TRUE(poll());
    ASSERT_TRUE(queue.tryPush(connected()));
    ASSERT_TRUE(poll());
    ASSERT_TRUE(queue.tryPush(axis(GamepadAxis::LeftX, std::numeric_limits<float>::quiet_NaN())));
    EXPECT_FALSE(poll());
    EXPECT_FLOAT_EQ(state->snapshots()[0].axis(GamepadAxis::LeftX), 0.0F);
    auto recovered = poll();
    ASSERT_TRUE(recovered);
    EXPECT_TRUE(recovered->gamepads().empty());
}

} // namespace
} // namespace Tina::Platform
