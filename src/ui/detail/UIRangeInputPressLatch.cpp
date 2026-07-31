#include "UIRangeInputPressLatch.hpp"

#include <tina/ui/UIErrors.hpp>

#include <variant>

namespace Tina::UI::Detail {

UIRangeInputPressLatch::UIRangeInputPressLatch(Platform::WindowId ownerWindow) noexcept
    : ownerWindow_(ownerWindow)
{
}

Core::Status UIRangeInputPressLatch::validateControl(const Platform::DigitalControlIdentity& control) const
{
    if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control); key != nullptr)
    {
        if (key->window == ownerWindow_ && keySlot(key->key).has_value())
        {
            return Core::success();
        }
    } else if (const auto* button = std::get_if<Platform::GamepadButtonControlIdentity>(&control);
               button != nullptr && button->routedWindow == ownerWindow_ && button->gamepad.hasValue() &&
               button->gamepad.index() < gamepads_.size() && gamepadButtonSlot(button->button).has_value())
    {
        return Core::success();
    }
    return Core::failure(UIErrorCode::InvalidControlValue,
                         "UI RangeInput command requires an owned Arrow or D-pad control");
}

bool UIRangeInputPressLatch::isLatched(const Platform::DigitalControlIdentity& control,
                                       UIRangeInputCommand command) const noexcept
{
    if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control); key != nullptr)
    {
        const auto slot = keySlot(key->key);
        return key->window == ownerWindow_ && slot.has_value() && keys_[*slot].latched &&
               keys_[*slot].command == command;
    }
    const auto* button = std::get_if<Platform::GamepadButtonControlIdentity>(&control);
    if (button == nullptr || button->routedWindow != ownerWindow_ || !button->gamepad.hasValue() ||
        button->gamepad.index() >= gamepads_.size())
    {
        return false;
    }
    const auto slot = gamepadButtonSlot(button->button);
    if (!slot.has_value())
    {
        return false;
    }
    const GamepadPresses& presses = gamepads_[button->gamepad.index()];
    return presses.gamepad == button->gamepad && presses.buttons[*slot].latched &&
           presses.buttons[*slot].command == command;
}

void UIRangeInputPressLatch::latch(const Platform::DigitalControlIdentity& control,
                                   UIRangeInputCommand command) noexcept
{
    if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control); key != nullptr)
    {
        const auto slot = keySlot(key->key);
        if (key->window == ownerWindow_ && slot.has_value())
        {
            keys_[*slot] = {.command = command, .latched = true};
        }
        return;
    }
    const auto* button = std::get_if<Platform::GamepadButtonControlIdentity>(&control);
    if (button == nullptr || button->routedWindow != ownerWindow_ || !button->gamepad.hasValue() ||
        button->gamepad.index() >= gamepads_.size())
    {
        return;
    }
    const auto slot = gamepadButtonSlot(button->button);
    if (!slot.has_value())
    {
        return;
    }
    GamepadPresses& presses = gamepads_[button->gamepad.index()];
    if (presses.gamepad != button->gamepad)
    {
        presses = {};
        presses.gamepad = button->gamepad;
    }
    presses.buttons[*slot] = {.command = command, .latched = true};
}

bool UIRangeInputPressLatch::release(const Platform::DigitalControlIdentity& control,
                                     UIRangeInputCommand command) noexcept
{
    if (!isLatched(control, command))
    {
        return false;
    }
    if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control); key != nullptr)
    {
        keys_[*keySlot(key->key)] = {};
        return true;
    }
    const auto& button = std::get<Platform::GamepadButtonControlIdentity>(control);
    gamepads_[button.gamepad.index()].buttons[*gamepadButtonSlot(button.button)] = {};
    return true;
}

void UIRangeInputPressLatch::clear() noexcept
{
    keys_.fill({});
    gamepads_.fill({});
}

void UIRangeInputPressLatch::clearGamepad(Platform::GamepadId gamepad) noexcept
{
    if (!gamepad.hasValue() || gamepad.index() >= gamepads_.size())
    {
        return;
    }
    GamepadPresses& presses = gamepads_[gamepad.index()];
    if (presses.gamepad == gamepad)
    {
        presses = {};
    }
}

std::optional<usize> UIRangeInputPressLatch::keySlot(Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Left:
        return 0;
    case Platform::Key::Right:
        return 1;
    case Platform::Key::Up:
        return 2;
    case Platform::Key::Down:
        return 3;
    default:
        return std::nullopt;
    }
}

std::optional<usize> UIRangeInputPressLatch::gamepadButtonSlot(Platform::GamepadButton button) noexcept
{
    switch (button)
    {
    case Platform::GamepadButton::DpadLeft:
        return 0;
    case Platform::GamepadButton::DpadRight:
        return 1;
    case Platform::GamepadButton::DpadUp:
        return 2;
    case Platform::GamepadButton::DpadDown:
        return 3;
    default:
        return std::nullopt;
    }
}

} // namespace Tina::UI::Detail
