#pragma once

#include <tina/platform/Input.hpp>

#include <optional>

struct GLFWgamepadstate;

namespace Tina::Platform::Detail {

// Default stick radial deadzone (applied to Left/Right stick axes only).
// Values inside the deadzone become 0; outside is rescaled to [-1, 1].
inline constexpr float DefaultGamepadStickDeadzone = 0.18F;

// Minimum absolute axis delta after filtering before a GamepadAxisTransition is
// emitted. Suppresses noise without inventing Down→Up edges.
inline constexpr float DefaultGamepadAxisChangeHysteresis = 0.02F;

// Maps GLFW standard gamepad button indices to Tina GamepadButton.
[[nodiscard]] std::optional<GamepadButton> translateGlfwGamepadButton(int glfwButton) noexcept;

// Maps GLFW standard gamepad axis indices to Tina GamepadAxis.
[[nodiscard]] std::optional<GamepadAxis> translateGlfwGamepadAxis(int glfwAxis) noexcept;

// Per-axis stick deadzone with outer rescaling. Triggers are left unchanged.
[[nodiscard]] float filterGamepadAxisValue(GamepadAxis axis, float rawValue,
                                           float stickDeadzone = DefaultGamepadStickDeadzone) noexcept;

// Returns true when the filtered axis should publish a transition relative to
// the last published value.
[[nodiscard]] bool gamepadAxisChanged(float previousPublished, float currentFiltered,
                                      float hysteresis = DefaultGamepadAxisChangeHysteresis) noexcept;

// Fills heldButtons and axes from a GLFW gamepadstate. Stick axes are deadzoned;
// unknown indices are skipped.
void applyGlfwGamepadState(
    GamepadSnapshot& snapshot,
    const GLFWgamepadstate& state,
    float stickDeadzone = DefaultGamepadStickDeadzone) noexcept;

} // namespace Tina::Platform::Detail
