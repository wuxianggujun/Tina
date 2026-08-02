#include "UIDefaultActionPressState.hpp"

#include <tina/ui/UIErrors.hpp>

#include <variant>

namespace Tina::UI::Detail {

UIDefaultActionPressState::UIDefaultActionPressState(
    Platform::WindowId ownerWindow) noexcept
    : ownerWindow_(ownerWindow)
{
}

Core::Status UIDefaultActionPressState::validateControl(
    UIButtonActivationSource source,
    const Platform::DigitalControlIdentity& control) const
{
    if (source == UIButtonActivationSource::Keyboard)
    {
        const auto* key = std::get_if<Platform::KeyControlIdentity>(&control);
        if (key != nullptr && isOwnedAcceptKey(*key))
        {
            return Core::success();
        }
    } else if (source == UIButtonActivationSource::Gamepad)
    {
        const auto* button =
            std::get_if<Platform::GamepadButtonControlIdentity>(&control);
        if (button != nullptr && isOwnedAcceptButton(*button))
        {
            return Core::success();
        }
    }
    return Core::failure(
        UIErrorCode::InvalidButtonAction,
        "UI default-action control does not match its activation source");
}

UINodeId UIDefaultActionPressState::pressedTarget(
    const Platform::DigitalControlIdentity& control) const noexcept
{
    if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control);
        key != nullptr)
    {
        const auto slot = keySlot(key->key);
        return key->window == ownerWindow_ && slot.has_value()
                   ? keyPressedTargets_[*slot]
                   : UINodeId{};
    }
    if (const auto* button =
            std::get_if<Platform::GamepadButtonControlIdentity>(&control);
        button != nullptr && isOwnedAcceptButton(*button))
    {
        return pressedTarget(button->gamepad);
    }
    return {};
}

UINodeId UIDefaultActionPressState::pressedTarget(
    Platform::GamepadId gamepad) const noexcept
{
    if (!gamepad.hasValue() || gamepad.index() >= gamepadPressed_.size())
    {
        return {};
    }
    const GamepadPress& pressed = gamepadPressed_[gamepad.index()];
    return pressed.gamepad == gamepad ? pressed.target : UINodeId{};
}

void UIDefaultActionPressState::setPressedTarget(
    const Platform::DigitalControlIdentity& control, UINodeId target) noexcept
{
    if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control);
        key != nullptr)
    {
        const auto slot = keySlot(key->key);
        if (key->window == ownerWindow_ && slot.has_value())
        {
            keyPressedTargets_[*slot] = target;
        }
        return;
    }
    const auto* button =
        std::get_if<Platform::GamepadButtonControlIdentity>(&control);
    if (button == nullptr || !isOwnedAcceptButton(*button))
    {
        return;
    }
    gamepadPressed_[button->gamepad.index()] = {
        .gamepad = button->gamepad,
        .target = target,
    };
}

void UIDefaultActionPressState::clearPressedTarget(
    const Platform::DigitalControlIdentity& control) noexcept
{
    if (const auto* key = std::get_if<Platform::KeyControlIdentity>(&control);
        key != nullptr)
    {
        const auto slot = keySlot(key->key);
        if (key->window == ownerWindow_ && slot.has_value())
        {
            keyPressedTargets_[*slot] = {};
        }
        return;
    }
    const auto* button =
        std::get_if<Platform::GamepadButtonControlIdentity>(&control);
    if (button != nullptr && isOwnedAcceptButton(*button))
    {
        clearGamepad(button->gamepad);
    }
}

void UIDefaultActionPressState::clearAll() noexcept
{
    keyPressedTargets_.fill({});
    gamepadPressed_.fill({});
}

void UIDefaultActionPressState::clearNode(UINodeId node) noexcept
{
    if (!node.hasValue())
    {
        return;
    }
    for (UINodeId& target : keyPressedTargets_)
    {
        if (target == node)
        {
            target = {};
        }
    }
    for (GamepadPress& pressed : gamepadPressed_)
    {
        if (pressed.target == node)
        {
            pressed = {};
        }
    }
}

void UIDefaultActionPressState::clearGamepad(
    Platform::GamepadId gamepad) noexcept
{
    if (!gamepad.hasValue() || gamepad.index() >= gamepadPressed_.size())
    {
        return;
    }
    GamepadPress& pressed = gamepadPressed_[gamepad.index()];
    if (pressed.gamepad == gamepad)
    {
        pressed = {};
    }
}

bool UIDefaultActionPressState::isPressed(UINodeId node) const noexcept
{
    if (!node.hasValue())
    {
        return false;
    }
    for (UINodeId target : keyPressedTargets_)
    {
        if (target == node)
        {
            return true;
        }
    }
    for (const GamepadPress& pressed : gamepadPressed_)
    {
        if (pressed.target == node)
        {
            return true;
        }
    }
    return false;
}

UIDefaultActionPressState::PressedTargets
UIDefaultActionPressState::pressedTargets() const noexcept
{
    PressedTargets targets{};
    usize targetIndex = 0;
    for (const UINodeId target : keyPressedTargets_)
    {
        targets[targetIndex++] = target;
    }
    for (const GamepadPress& pressed : gamepadPressed_)
    {
        targets[targetIndex++] = pressed.target;
    }
    return targets;
}

std::optional<usize>
UIDefaultActionPressState::keySlot(Platform::Key key) noexcept
{
    switch (key)
    {
    case Platform::Key::Enter:
        return 0;
    case Platform::Key::Space:
        return 1;
    case Platform::Key::KeypadEnter:
        return 2;
    default:
        return std::nullopt;
    }
}

bool UIDefaultActionPressState::isOwnedAcceptKey(
    const Platform::KeyControlIdentity& key) const noexcept
{
    return key.window == ownerWindow_ && keySlot(key.key).has_value();
}

bool UIDefaultActionPressState::isOwnedAcceptButton(
    const Platform::GamepadButtonControlIdentity& button) const noexcept
{
    return button.routedWindow == ownerWindow_ && button.gamepad.hasValue() &&
           button.gamepad.index() < gamepadPressed_.size() &&
           button.button == Platform::GamepadButton::South;
}

} // namespace Tina::UI::Detail
