#include "GalleryActions.hpp"

#include <tina/platform/Input.hpp>

namespace Tina::Gallery {
namespace {

void bindKey(EngineConfig& config, Platform::Key key, InputActionId action) noexcept
{
    config.inputActions.bindings.push_back(InputActionBinding{
        .input = PrimaryWindowKeyBinding{.key = key},
        .action = action,
        // Frame rather than Simulation: menu navigation is a UI concern read once per frame, and putting
        // it on the fixed tick would make selection speed depend on the accumulator.
        .domain = InputActionDomain::Frame,
    });
}

void bindGamepad(EngineConfig& config, Platform::GamepadButton button, InputActionId action) noexcept
{
    config.inputActions.bindings.push_back(InputActionBinding{
        // binding id left unset on purpose: ActionMapper assigns one automatically, and hand-numbering
        // them here would collide with whatever the host binds around these.
        .input = StandardGamepadButtonBinding{.button = button},
        .action = action,
        .domain = InputActionDomain::Frame,
    });
}

} // namespace

void appendGalleryBindings(EngineConfig& config) noexcept
{
    // Escape covers Android's BACK, which androidKeyFromKeyCode maps to Escape precisely so a mobile
    // back gesture and a desktop Escape are one intent rather than two spellings.
    bindKey(config, Platform::Key::Escape, GalleryBackAction);
    bindKey(config, Platform::Key::Up, GalleryUpAction);
    bindKey(config, Platform::Key::Down, GalleryDownAction);
    bindKey(config, Platform::Key::Enter, GalleryActivateAction);

    // A gamepad drives the gallery too, because the engine's own D-pad and South-button conventions
    // already exist and a sample set that ignored them would not exercise them anywhere.
    bindGamepad(config, Platform::GamepadButton::East, GalleryBackAction);
    bindGamepad(config, Platform::GamepadButton::DpadUp, GalleryUpAction);
    bindGamepad(config, Platform::GamepadButton::DpadDown, GalleryDownAction);
    bindGamepad(config, Platform::GamepadButton::South, GalleryActivateAction);
}

} // namespace Tina::Gallery
