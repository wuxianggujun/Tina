#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>
#include <string_view>

namespace Tina::UI {

enum class UIToggleSwitchSize : u8 {
    Compact = 0,
    Standard,
};

// ToggleSwitch is the switch authoring/semantics profile of the existing
// Toggle behavior. accessibleName is published by the control root; no text or
// decorative child is needed.
struct UIToggleSwitchConfig final {
    std::string_view accessibleName{};
    UIToggleSwitchSize size = UIToggleSwitchSize::Standard;

    auto operator<=>(const UIToggleSwitchConfig&) const = default;
};

} // namespace Tina::UI
