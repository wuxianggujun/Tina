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

// A product cannot draw correct button glyphs without knowing the legend, and
// guessing wrongly is worse than showing a neutral prompt, so unknown pads must
// classify as Generic rather than defaulting to a popular vendor.
TEST(GlfwGamepadTranslationTests, ClassifiesLayoutFromGuidVendorThenName)
{
    using Platform::GamepadLayout;
    using Platform::Detail::classifyGamepadLayout;

    // SDL GUIDs carry the little-endian USB vendor id in bytes 8..11, so "4c05"
    // is vendor 0x054C (Sony). The GUID is checked before the name because driver
    // and OS names vary while the vendor id does not.
    EXPECT_EQ(classifyGamepadLayout({}, "030000004c050000cc09000011810000"),
              GamepadLayout::PlayStation);
    EXPECT_EQ(classifyGamepadLayout({}, "030000005e040000e002000003090000"), GamepadLayout::Xbox);
    EXPECT_EQ(classifyGamepadLayout({}, "030000007e0500000920000011810000"), GamepadLayout::Nintendo);

    // The GUID wins over a contradicting name.
    EXPECT_EQ(classifyGamepadLayout("Xbox 360 Controller", "030000004c050000cc09000011810000"),
              GamepadLayout::PlayStation);

    // Name is the fallback when the GUID carries no known vendor.
    EXPECT_EQ(classifyGamepadLayout("Xbox Series Pad", "00000000000000000000000000000000"),
              GamepadLayout::Xbox);
    EXPECT_EQ(classifyGamepadLayout("Sony DualSense Wireless", {}), GamepadLayout::PlayStation);
    EXPECT_EQ(classifyGamepadLayout("Nintendo Switch Pro Controller", {}), GamepadLayout::Nintendo);
    // Matching is case-insensitive because names differ by driver.
    EXPECT_EQ(classifyGamepadLayout("generic xinput device", {}), GamepadLayout::Xbox);

    EXPECT_EQ(classifyGamepadLayout("Some Unknown Pad", {}), GamepadLayout::Generic);
    EXPECT_EQ(classifyGamepadLayout({}, {}), GamepadLayout::Generic);
    // A truncated GUID must not be read past its end.
    EXPECT_EQ(classifyGamepadLayout({}, "0300"), GamepadLayout::Generic);
}

// Identity is presentation-only, so an over-long name is truncated rather than
// failing the connect event that carries it.
TEST(GlfwGamepadTranslationTests, DeviceIdentityCopiesAndTruncates)
{
    EXPECT_TRUE(Platform::Detail::makeGamepadName(nullptr).view().empty());
    EXPECT_TRUE(Platform::Detail::makeGamepadGuid(nullptr).view().empty());
    EXPECT_EQ(Platform::Detail::makeGamepadName("Pad").view(), "Pad");

    const std::string overlong(Platform::GamepadNameCapacity + 32U, 'x');
    const Platform::GamepadName name = Platform::Detail::makeGamepadName(overlong.c_str());
    EXPECT_EQ(name.view().size(), Platform::GamepadNameCapacity);

    // The GUID buffer reserves room for a terminator, so it holds one less.
    const std::string longGuid(Platform::GamepadGuidCapacity + 8U, 'a');
    const Platform::GamepadGuid guid = Platform::Detail::makeGamepadGuid(longGuid.c_str());
    EXPECT_EQ(guid.view().size(), Platform::GamepadGuidCapacity - 1U);
}

} // namespace Tina::Tests
