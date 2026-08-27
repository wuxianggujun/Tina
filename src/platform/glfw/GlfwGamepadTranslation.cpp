#include "GlfwGamepadTranslation.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <string_view>

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

GamepadLayout classifyGamepadLayout(std::string_view name, std::string_view guid) noexcept
{
    // SDL GUID layout places the little-endian USB vendor id in bytes 8..11, so
    // "030000004c050000..." carries vendor 0x054C. This is checked before the
    // name because names vary per driver and OS while the vendor id does not.
    if (guid.size() >= 12U) {
        const std::string_view vendorField = guid.substr(8U, 4U);
        const auto hexValue = [](char digit) noexcept -> int {
            if (digit >= '0' && digit <= '9') {
                return digit - '0';
            }
            if (digit >= 'a' && digit <= 'f') {
                return digit - 'a' + 10;
            }
            if (digit >= 'A' && digit <= 'F') {
                return digit - 'A' + 10;
            }
            return -1;
        };
        int nibbles[4]{};
        bool parsed = true;
        for (usize index = 0; index < 4U; ++index) {
            nibbles[index] = hexValue(vendorField[index]);
            parsed = parsed && nibbles[index] >= 0;
        }
        if (parsed) {
            // Bytes are little-endian pairs: "4c05" means 0x054C.
            const unsigned vendor = static_cast<unsigned>((nibbles[2] << 12) | (nibbles[3] << 8) |
                                                          (nibbles[0] << 4) | nibbles[1]);
            switch (vendor) {
            case 0x045EU:
                return GamepadLayout::Xbox;
            case 0x054CU:
                return GamepadLayout::PlayStation;
            case 0x057EU:
                return GamepadLayout::Nintendo;
            default:
                break;
            }
        }
    }

    const auto containsInsensitive = [name](std::string_view needle) noexcept {
        if (needle.size() > name.size()) {
            return false;
        }
        const auto lower = [](char value) noexcept {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
        };
        for (usize offset = 0; offset + needle.size() <= name.size(); ++offset) {
            bool matched = true;
            for (usize index = 0; index < needle.size(); ++index) {
                if (lower(name[offset + index]) != lower(needle[index])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return true;
            }
        }
        return false;
    };
    if (containsInsensitive("xbox") || containsInsensitive("xinput")) {
        return GamepadLayout::Xbox;
    }
    if (containsInsensitive("playstation") || containsInsensitive("dualshock") ||
        containsInsensitive("dualsense") || containsInsensitive("ps4") ||
        containsInsensitive("ps5")) {
        return GamepadLayout::PlayStation;
    }
    if (containsInsensitive("nintendo") || containsInsensitive("switch") ||
        containsInsensitive("joy-con")) {
        return GamepadLayout::Nintendo;
    }
    return GamepadLayout::Generic;
}

GamepadName makeGamepadName(const char* name) noexcept
{
    GamepadName result{};
    if (name == nullptr) {
        return result;
    }
    usize length = 0;
    while (name[length] != '\0' && length < GamepadNameCapacity) {
        result.bytes[length] = name[length];
        ++length;
    }
    result.length = static_cast<u8>(length);
    return result;
}

GamepadGuid makeGamepadGuid(const char* guid) noexcept
{
    GamepadGuid result{};
    if (guid == nullptr) {
        return result;
    }
    usize length = 0;
    while (guid[length] != '\0' && length + 1U < GamepadGuidCapacity) {
        result.bytes[length] = guid[length];
        ++length;
    }
    result.length = static_cast<u8>(length);
    return result;
}

bool gamepadIdentityChanged(const GamepadDeviceInfo& previous,
                            const GamepadDeviceInfo& current) noexcept
{
    // A driver that reports nothing is not evidence of a swap, and treating it as
    // one would fabricate a disconnect/connect pair on every poll.
    if (current.guid.length == 0 && current.name.length == 0) {
        return false;
    }
    if (current.guid.length != 0 && previous.guid.length != 0
        && current.guid.view() != previous.guid.view()) {
        return true;
    }
    return current.name.length != 0 && previous.name.length != 0
        && current.name.view() != previous.name.view();
}

} // namespace Tina::Platform::Detail
