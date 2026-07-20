#include <gtest/gtest.h>

#include "GlfwGamepadTranslation.hpp"

#include <GLFW/glfw3.h>

namespace Tina::Tests {

TEST(GlfwGamepadTranslationTests, MapsStandardButtonsAndAxes)
{
    EXPECT_EQ(
        Platform::Detail::translateGlfwGamepadButton(GLFW_GAMEPAD_BUTTON_A),
        Platform::GamepadButton::South);
    EXPECT_EQ(
        Platform::Detail::translateGlfwGamepadButton(GLFW_GAMEPAD_BUTTON_B),
        Platform::GamepadButton::East);
    EXPECT_EQ(
        Platform::Detail::translateGlfwGamepadButton(GLFW_GAMEPAD_BUTTON_DPAD_UP),
        Platform::GamepadButton::DpadUp);
    EXPECT_EQ(
        Platform::Detail::translateGlfwGamepadAxis(GLFW_GAMEPAD_AXIS_LEFT_X),
        Platform::GamepadAxis::LeftX);
    EXPECT_EQ(
        Platform::Detail::translateGlfwGamepadAxis(GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER),
        Platform::GamepadAxis::RightTrigger);
    EXPECT_FALSE(Platform::Detail::translateGlfwGamepadButton(999).has_value());
}

TEST(GlfwGamepadTranslationTests, ApplyStateCopiesHeldButtonsAndAxes)
{
    GLFWgamepadstate state{};
    state.buttons[GLFW_GAMEPAD_BUTTON_A] = GLFW_PRESS;
    state.buttons[GLFW_GAMEPAD_BUTTON_START] = GLFW_PRESS;
    state.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = 0.5F;
    state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = -0.25F;

    Platform::GamepadSnapshot snapshot{};
    Platform::Detail::applyGlfwGamepadState(snapshot, state);
    EXPECT_TRUE(snapshot.isHeld(Platform::GamepadButton::South));
    EXPECT_TRUE(snapshot.isHeld(Platform::GamepadButton::Start));
    EXPECT_FALSE(snapshot.isHeld(Platform::GamepadButton::East));
    EXPECT_FLOAT_EQ(snapshot.axis(Platform::GamepadAxis::LeftX), 0.5F);
    EXPECT_FLOAT_EQ(snapshot.axis(Platform::GamepadAxis::LeftTrigger), -0.25F);
}

} // namespace Tina::Tests
