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
    // Above default stick deadzone (0.18) after rescale: 0.5 is kept nonzero.
    state.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = 0.5F;
    state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = -0.25F;

    Platform::GamepadSnapshot snapshot{};
    Platform::Detail::applyGlfwGamepadState(snapshot, state);
    EXPECT_TRUE(snapshot.isHeld(Platform::GamepadButton::South));
    EXPECT_TRUE(snapshot.isHeld(Platform::GamepadButton::Start));
    EXPECT_FALSE(snapshot.isHeld(Platform::GamepadButton::East));
    EXPECT_NEAR(
        snapshot.axis(Platform::GamepadAxis::LeftX),
        (0.5F - 0.18F) / (1.0F - 0.18F),
        1.0e-5F);
    EXPECT_FLOAT_EQ(snapshot.axis(Platform::GamepadAxis::LeftTrigger), -0.25F);
}

TEST(GlfwGamepadTranslationTests, StickDeadzoneZerosSmallValuesAndRescales)
{
    using Platform::Detail::filterGamepadAxisValue;
    using Platform::Detail::DefaultGamepadStickDeadzone;
    using Platform::GamepadAxis;

    EXPECT_FLOAT_EQ(
        filterGamepadAxisValue(GamepadAxis::LeftX, 0.10F, DefaultGamepadStickDeadzone),
        0.0F);
    EXPECT_FLOAT_EQ(
        filterGamepadAxisValue(GamepadAxis::LeftY, -0.18F, DefaultGamepadStickDeadzone),
        0.0F);
    EXPECT_NEAR(
        filterGamepadAxisValue(GamepadAxis::RightX, 1.0F, DefaultGamepadStickDeadzone),
        1.0F,
        1.0e-5F);
    EXPECT_NEAR(
        filterGamepadAxisValue(GamepadAxis::LeftX, 0.59F, DefaultGamepadStickDeadzone),
        (0.59F - 0.18F) / (1.0F - 0.18F),
        1.0e-5F);
    // Triggers are not deadzoned as sticks.
    EXPECT_FLOAT_EQ(
        filterGamepadAxisValue(GamepadAxis::LeftTrigger, 0.05F, DefaultGamepadStickDeadzone),
        0.05F);
}

TEST(GlfwGamepadTranslationTests, AxisChangeHysteresisSuppressesTinyNoise)
{
    using Platform::Detail::gamepadAxisChanged;
    using Platform::Detail::DefaultGamepadAxisChangeHysteresis;

    EXPECT_FALSE(gamepadAxisChanged(0.50F, 0.51F, DefaultGamepadAxisChangeHysteresis));
    EXPECT_TRUE(gamepadAxisChanged(0.50F, 0.55F, DefaultGamepadAxisChangeHysteresis));
    EXPECT_TRUE(gamepadAxisChanged(0.10F, 0.0F, DefaultGamepadAxisChangeHysteresis));
    EXPECT_TRUE(gamepadAxisChanged(0.0F, 0.10F, DefaultGamepadAxisChangeHysteresis));
    EXPECT_FALSE(gamepadAxisChanged(0.0F, 0.0F, DefaultGamepadAxisChangeHysteresis));
}

TEST(GlfwGamepadTranslationTests, ApplyStateZerosStickNoiseInsideDeadzone)
{
    GLFWgamepadstate state{};
    state.axes[GLFW_GAMEPAD_AXIS_LEFT_X] = 0.05F;
    state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] = -0.12F;
    state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X] = 0.20F;

    Platform::GamepadSnapshot snapshot{};
    Platform::Detail::applyGlfwGamepadState(snapshot, state);
    EXPECT_FLOAT_EQ(snapshot.axis(Platform::GamepadAxis::LeftX), 0.0F);
    EXPECT_FLOAT_EQ(snapshot.axis(Platform::GamepadAxis::LeftY), 0.0F);
    EXPECT_NEAR(
        snapshot.axis(Platform::GamepadAxis::RightX),
        (0.20F - 0.18F) / (1.0F - 0.18F),
        1.0e-5F);
}

} // namespace Tina::Tests
