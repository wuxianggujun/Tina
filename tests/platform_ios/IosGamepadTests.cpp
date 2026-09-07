#include <tina/platform/ios/IosSession.hpp>

#include <gtest/gtest.h>

namespace Tina::Platform {
namespace {

TEST(IosGamepadTest, SessionPublishesNativeControlsAndDisconnects)
{
    auto created = IosSession::Create();
    ASSERT_TRUE(created);
    auto& session = **created;
    ASSERT_TRUE(session.onGamepadConnected(10, "DualSense", "DualSense"));
    ASSERT_TRUE(session.bindLayer({0x1000}, {800, 600}, {2.0F, 2.0F}));
    ASSERT_TRUE(session.onGamepadButton(10, GamepadButton::South, DigitalTransition::Down));
    ASSERT_TRUE(session.onGamepadAxis(10, GamepadAxis::LeftTrigger, 0.0F));
    ASSERT_TRUE(session.onGamepadAxis(10, GamepadAxis::RightY, -1.0F));
    auto first = session.pollFrame();
    ASSERT_TRUE(first);
    ASSERT_NE(first->frame(), nullptr);
    ASSERT_EQ(first->frame()->gamepads().size(), 1U);
    const auto id = first->frame()->gamepads()[0].gamepad;
    EXPECT_TRUE(first->frame()->gamepads()[0].isHeld(GamepadButton::South));
    EXPECT_FLOAT_EQ(first->frame()->gamepads()[0].axis(GamepadAxis::LeftTrigger), -1.0F);
    EXPECT_FLOAT_EQ(first->frame()->gamepads()[0].axis(GamepadAxis::RightY), -1.0F);
    ASSERT_TRUE(session.onGamepadDisconnected(10));
    auto disconnected = session.pollFrame();
    ASSERT_TRUE(disconnected);
    EXPECT_TRUE(disconnected->frame()->gamepads().empty());
    const auto* cancel = std::get_if<InputCancelTransition>(&disconnected->frame()->inputTransitions()[0].payload);
    ASSERT_NE(cancel, nullptr);
    EXPECT_EQ(cancel->gamepad, id);
}

TEST(IosGamepadTest, LayerLossClearsControlsButDoesNotDiscardDeviceRemoval)
{
    auto created = IosSession::Create();
    ASSERT_TRUE(created);
    auto& session = **created;
    ASSERT_TRUE(session.bindLayer({0x1000}, {800, 600}, {2.0F, 2.0F}));
    ASSERT_TRUE(session.onGamepadConnected(10, "Controller", ""));
    ASSERT_TRUE(session.onGamepadButton(10, GamepadButton::South, DigitalTransition::Down));
    ASSERT_TRUE(session.pollFrame());
    session.unbindLayer();
    ASSERT_TRUE(session.onGamepadDisconnected(10));
    auto suspended = session.pollFrame();
    ASSERT_TRUE(suspended);
    EXPECT_TRUE(suspended->frame()->gamepads().empty());
    EXPECT_TRUE(session.takeGamepadResyncRequest());
    ASSERT_TRUE(session.bindLayer({0x2000}, {800, 600}, {2.0F, 2.0F}));
    ASSERT_TRUE(session.pollFrame());
    ASSERT_TRUE(session.onGamepadConnected(10, "Controller", ""));
    auto resumed = session.pollFrame();
    ASSERT_TRUE(resumed);
    ASSERT_EQ(resumed->frame()->gamepads().size(), 1U);
    EXPECT_FALSE(resumed->frame()->gamepads()[0].isHeld(GamepadButton::South));
    session.shutdown();
    EXPECT_FALSE(session.onGamepadButton(10, GamepadButton::South, DigitalTransition::Down));
}

} // namespace
} // namespace Tina::Platform
