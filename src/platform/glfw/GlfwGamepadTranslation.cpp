#include "GlfwGamepadTranslation.hpp"

#include <GLFW/glfw3.h>

namespace Tina::Platform::Detail {

std::optional<GamepadButton> translateGlfwGamepadButton(int glfwButton) noexcept
{
    switch (glfwButton) {
    case GLFW_GAMEPAD_BUTTON_A:
        return GamepadButton::South;
    case GLFW_GAMEPAD_BUTTON_B:
        return GamepadButton::East;
    case GLFW_GAMEPAD_BUTTON_X:
        return GamepadButton::West;
    case GLFW_GAMEPAD_BUTTON_Y:
        return GamepadButton::North;
    case GLFW_GAMEPAD_BUTTON_LEFT_BUMPER:
        return GamepadButton::LeftBumper;
    case GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER:
        return GamepadButton::RightBumper;
    case GLFW_GAMEPAD_BUTTON_BACK:
        return GamepadButton::Back;
    case GLFW_GAMEPAD_BUTTON_START:
        return GamepadButton::Start;
    case GLFW_GAMEPAD_BUTTON_GUIDE:
        return GamepadButton::Guide;
    case GLFW_GAMEPAD_BUTTON_LEFT_THUMB:
        return GamepadButton::LeftStick;
    case GLFW_GAMEPAD_BUTTON_RIGHT_THUMB:
        return GamepadButton::RightStick;
    case GLFW_GAMEPAD_BUTTON_DPAD_UP:
        return GamepadButton::DpadUp;
    case GLFW_GAMEPAD_BUTTON_DPAD_RIGHT:
        return GamepadButton::DpadRight;
    case GLFW_GAMEPAD_BUTTON_DPAD_DOWN:
        return GamepadButton::DpadDown;
    case GLFW_GAMEPAD_BUTTON_DPAD_LEFT:
        return GamepadButton::DpadLeft;
    default:
        return std::nullopt;
    }
}

std::optional<GamepadAxis> translateGlfwGamepadAxis(int glfwAxis) noexcept
{
    switch (glfwAxis) {
    case GLFW_GAMEPAD_AXIS_LEFT_X:
        return GamepadAxis::LeftX;
    case GLFW_GAMEPAD_AXIS_LEFT_Y:
        return GamepadAxis::LeftY;
    case GLFW_GAMEPAD_AXIS_RIGHT_X:
        return GamepadAxis::RightX;
    case GLFW_GAMEPAD_AXIS_RIGHT_Y:
        return GamepadAxis::RightY;
    case GLFW_GAMEPAD_AXIS_LEFT_TRIGGER:
        return GamepadAxis::LeftTrigger;
    case GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return GamepadAxis::RightTrigger;
    default:
        return std::nullopt;
    }
}

void applyGlfwGamepadState(GamepadSnapshot& snapshot, const GLFWgamepadstate& state) noexcept
{
    snapshot.heldButtons.reset();
    snapshot.axes.fill(0.0F);

    for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
        const auto mapped = translateGlfwGamepadButton(button);
        if (!mapped.has_value()) {
            continue;
        }
        if (state.buttons[button] == GLFW_PRESS) {
            snapshot.heldButtons.set(static_cast<usize>(*mapped));
        }
    }

    for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
        const auto mapped = translateGlfwGamepadAxis(axis);
        if (!mapped.has_value()) {
            continue;
        }
        snapshot.axes[static_cast<usize>(*mapped)] = state.axes[axis];
    }
}

} // namespace Tina::Platform::Detail
