#pragma once

#include <tina/platform/Input.hpp>

#include <string_view>

namespace Tina::Platform {

// Maps a DOM KeyboardEvent.code to a Tina Key.
//
// code is used rather than key because code names the physical key and does not
// change with the active keyboard layout or modifier state: WASD stays WASD on an
// AZERTY layout, and Shift+1 still reports Digit1. key would report "!" there,
// which no physical-key binding can match.
//
// Returns Key::Unknown for codes Tina has no slot for, which the caller must drop
// rather than publish -- the frame builder rejects Key::Unknown outright.
[[nodiscard]] Key html5KeyFromDomCode(std::string_view domCode) noexcept;

} // namespace Tina::Platform
