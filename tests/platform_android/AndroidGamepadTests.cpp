#include <tina/platform/android/AndroidPlatformFactory.hpp>

#include <gtest/gtest.h>

namespace Tina::Platform {
namespace {

TEST(AndroidGamepadTest, PublishesGamepadsAndKeepsDisconnectAcrossSurfaceLoss)
{
    AndroidPlatformBackendCreateParams params{};
    params.window = {0x1000};
    params.framebufferExtent = {1080, 2400};
    params.contentScale = {3.0F, 3.0F};
    params.gamepadEvents = std::make_shared<MobileGamepadEventQueue>();
    auto backend = createAndroidWindowSurfacePlatformBackend(params);
    ASSERT_TRUE(backend);
    ASSERT_TRUE(params.gamepadEvents->tryPush({.kind = MobileGamepadEventKind::Connected, .deviceId = 5}));
    ASSERT_TRUE(params.gamepadEvents->tryPush({.kind = MobileGamepadEventKind::Button, .deviceId = 5,
                                              .button = GamepadButton::South, .state = DigitalTransition::Down}));
    auto first = (*backend)->pollFrame();
    ASSERT_TRUE(first);
    ASSERT_EQ(first->frame()->gamepads().size(), 1U);
    EXPECT_TRUE(first->frame()->gamepads()[0].isHeld(GamepadButton::South));
    EXPECT_FLOAT_EQ(first->frame()->gamepads()[0].axis(GamepadAxis::RightTrigger), -1.0F);
    auto* facet = dynamic_cast<IAndroidPlatformBackend*>(backend->get());
    ASSERT_NE(facet, nullptr);
    ASSERT_TRUE(facet->onNativeWindowDestroyed());
    ASSERT_TRUE(params.gamepadEvents->tryPush({.kind = MobileGamepadEventKind::Disconnected, .deviceId = 5}));
    auto suspended = (*backend)->pollFrame();
    ASSERT_TRUE(suspended);
    EXPECT_TRUE(suspended->frame()->gamepads().empty());
    EXPECT_TRUE(params.gamepadEvents->takeResyncRequest());
}

} // namespace
} // namespace Tina::Platform
