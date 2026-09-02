#include "Html5KeyTranslation.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace Tina::Platform {
namespace {

struct DomCodeMapping final {
    std::string_view domCode;
    Key key;
};

// Sorted by domCode so lookup is a binary search. Kept as one table rather than a
// chain of comparisons because a keydown storm during text entry hits this per event.
constexpr std::array<DomCodeMapping, 108> DomCodeMappings{{
    {"AltLeft", Key::LeftAlt},
    {"AltRight", Key::RightAlt},
    {"ArrowDown", Key::Down},
    {"ArrowLeft", Key::Left},
    {"ArrowRight", Key::Right},
    {"ArrowUp", Key::Up},
    {"Backquote", Key::GraveAccent},
    {"Backslash", Key::Backslash},
    {"Backspace", Key::Backspace},
    {"BracketLeft", Key::LeftBracket},
    {"BracketRight", Key::RightBracket},
    {"CapsLock", Key::CapsLock},
    {"Comma", Key::Comma},
    {"ContextMenu", Key::Menu},
    {"ControlLeft", Key::LeftControl},
    {"ControlRight", Key::RightControl},
    {"Delete", Key::Delete},
    {"Digit0", Key::Digit0},
    {"Digit1", Key::Digit1},
    {"Digit2", Key::Digit2},
    {"Digit3", Key::Digit3},
    {"Digit4", Key::Digit4},
    {"Digit5", Key::Digit5},
    {"Digit6", Key::Digit6},
    {"Digit7", Key::Digit7},
    {"Digit8", Key::Digit8},
    {"Digit9", Key::Digit9},
    {"End", Key::End},
    {"Enter", Key::Enter},
    {"Equal", Key::Equal},
    {"Escape", Key::Escape},
    {"F1", Key::F1},
    {"F10", Key::F10},
    {"F11", Key::F11},
    {"F12", Key::F12},
    {"F2", Key::F2},
    {"F3", Key::F3},
    {"F4", Key::F4},
    {"F5", Key::F5},
    {"F6", Key::F6},
    {"F7", Key::F7},
    {"F8", Key::F8},
    {"F9", Key::F9},
    {"Home", Key::Home},
    {"Insert", Key::Insert},
    {"IntlBackslash", Key::Backslash},
    {"KeyA", Key::A},
    {"KeyB", Key::B},
    {"KeyC", Key::C},
    {"KeyD", Key::D},
    {"KeyE", Key::E},
    {"KeyF", Key::F},
    {"KeyG", Key::G},
    {"KeyH", Key::H},
    {"KeyI", Key::I},
    {"KeyJ", Key::J},
    {"KeyK", Key::K},
    {"KeyL", Key::L},
    {"KeyM", Key::M},
    {"KeyN", Key::N},
    {"KeyO", Key::O},
    {"KeyP", Key::P},
    {"KeyQ", Key::Q},
    {"KeyR", Key::R},
    {"KeyS", Key::S},
    {"KeyT", Key::T},
    {"KeyU", Key::U},
    {"KeyV", Key::V},
    {"KeyW", Key::W},
    {"KeyX", Key::X},
    {"KeyY", Key::Y},
    {"KeyZ", Key::Z},
    {"MetaLeft", Key::LeftSuper},
    {"MetaRight", Key::RightSuper},
    {"Minus", Key::Minus},
    {"NumLock", Key::NumLock},
    {"Numpad0", Key::Keypad0},
    {"Numpad1", Key::Keypad1},
    {"Numpad2", Key::Keypad2},
    {"Numpad3", Key::Keypad3},
    {"Numpad4", Key::Keypad4},
    {"Numpad5", Key::Keypad5},
    {"Numpad6", Key::Keypad6},
    {"Numpad7", Key::Keypad7},
    {"Numpad8", Key::Keypad8},
    {"Numpad9", Key::Keypad9},
    {"NumpadAdd", Key::KeypadAdd},
    {"NumpadDecimal", Key::KeypadDecimal},
    {"NumpadDivide", Key::KeypadDivide},
    {"NumpadEnter", Key::KeypadEnter},
    {"NumpadEqual", Key::KeypadEqual},
    {"NumpadMultiply", Key::KeypadMultiply},
    {"NumpadSubtract", Key::KeypadSubtract},
    {"OSLeft", Key::LeftSuper},
    {"OSRight", Key::RightSuper},
    {"PageDown", Key::PageDown},
    {"PageUp", Key::PageUp},
    {"Pause", Key::Pause},
    {"Period", Key::Period},
    {"PrintScreen", Key::PrintScreen},
    {"Quote", Key::Apostrophe},
    {"ScrollLock", Key::ScrollLock},
    {"Semicolon", Key::Semicolon},
    {"ShiftLeft", Key::LeftShift},
    {"ShiftRight", Key::RightShift},
    {"Slash", Key::Slash},
    {"Space", Key::Space},
    {"Tab", Key::Tab},
}};

// A table that is not sorted silently breaks the binary search below, and the symptom
// is a few keys that never work rather than an obvious failure.
constexpr bool isSortedByDomCode() noexcept
{
    for (usize index = 1; index < DomCodeMappings.size(); ++index)
    {
        if (!(DomCodeMappings[index - 1].domCode < DomCodeMappings[index].domCode))
        {
            return false;
        }
    }
    return true;
}

static_assert(isSortedByDomCode(), "DomCodeMappings must stay sorted by domCode");

} // namespace

Key html5KeyFromDomCode(std::string_view domCode) noexcept
{
    if (domCode.empty())
    {
        return Key::Unknown;
    }
    const auto position = std::ranges::lower_bound(DomCodeMappings, domCode, {}, &DomCodeMapping::domCode);
    if (position == DomCodeMappings.end() || position->domCode != domCode)
    {
        return Key::Unknown;
    }
    return position->key;
}

} // namespace Tina::Platform
