#include "GlfwGamepadTranslation.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace Tina::Platform::Detail {
namespace {

[[nodiscard]] bool isStickAxis(GamepadAxis axis) noexcept
{
    return axis == GamepadAxis::LeftX || axis == GamepadAxis::LeftY
        || axis == GamepadAxis::RightX || axis == GamepadAxis::RightY;
}

[[nodiscard]] float clampUnit(float value) noexcept
{
    return std::clamp(value, -1.0F, 1.0F);
}

} // namespace

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

float filterGamepadAxisValue(GamepadAxis axis, float rawValue, float stickDeadzone) noexcept
{
    if (!std::isfinite(rawValue)) {
        return 0.0F;
    }
    const float value = clampUnit(rawValue);
    if (!isStickAxis(axis)) {
        return value;
    }
    const float deadzone = std::clamp(stickDeadzone, 0.0F, 0.95F);
    const float absValue = std::abs(value);
    if (absValue <= deadzone) {
        return 0.0F;
    }
    // Rescale remaining range so full deflection still reaches ±1.
    const float sign = value < 0.0F ? -1.0F : 1.0F;
    const float scaled = (absValue - deadzone) / (1.0F - deadzone);
    return clampUnit(sign * scaled);
}

bool gamepadAxisChanged(float previousPublished, float currentFiltered, float hysteresis) noexcept
{
    if (!std::isfinite(previousPublished) || !std::isfinite(currentFiltered)) {
        return previousPublished != currentFiltered;
    }
    if (previousPublished == currentFiltered) {
        return false;
    }
    // Always publish enter/leave of true zero so rest positions are reliable.
    if (previousPublished == 0.0F || currentFiltered == 0.0F) {
        return true;
    }
    const float threshold = std::max(0.0F, hysteresis);
    return std::abs(currentFiltered - previousPublished) >= threshold;
}

void applyGlfwGamepadState(
    GamepadSnapshot& snapshot,
    const GLFWgamepadstate& state,
    float stickDeadzone) noexcept
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
        snapshot.axes[static_cast<usize>(*mapped)] =
            filterGamepadAxisValue(*mapped, state.axes[axis], stickDeadzone);
    }
}

} // namespace Tina::Platform::Detail
