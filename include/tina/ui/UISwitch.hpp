#pragma once

#include <tina/core/base/Types.hpp>

#include <compare>
#include <string_view>

namespace Tina::UI {

enum class UISwitchSize : u8 {
    Compact = 0,
    Standard,
};

// Switch is the switch authoring/semantics profile of the existing
// Toggle behavior. accessibleName is published by the control root; no text or
// decorative child is needed.
struct UISwitchConfig final {
    std::string_view accessibleName{};
    UISwitchSize size = UISwitchSize::Standard;

    auto operator<=>(const UISwitchConfig&) const = default;
};

} // namespace Tina::UI
