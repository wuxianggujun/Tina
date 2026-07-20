#pragma once

#include <tina/platform/Input.hpp>

#include <optional>

struct GLFWgamepadstate;

namespace Tina::Platform::Detail {

// Maps GLFW standard gamepad button indices to Tina GamepadButton.
[[nodiscard]] std::optional<GamepadButton> translateGlfwGamepadButton(int glfwButton) noexcept;

// Maps GLFW standard gamepad axis indices to Tina GamepadAxis.
[[nodiscard]] std::optional<GamepadAxis> translateGlfwGamepadAxis(int glfwAxis) noexcept;

// Fills heldButtons and axes from a GLFW gamepadstate. Unknown indices are skipped.
void applyGlfwGamepadState(
    GamepadSnapshot& snapshot,
    const GLFWgamepadstate& state) noexcept;

} // namespace Tina::Platform::Detail
