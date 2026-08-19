#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

// Dropdown-only chrome. Normal/hover/pressed/focus fill and borders continue to
// use UIButtonPaint so Dropdown and DropdownItem share the mature Button path.
struct UIDropdownPaint final {
    UIStraightSrgba8Color indicatorColor{};
    UIStraightSrgba8Color openBackgroundColor{};
    UIStraightSrgba8Color selectedItemBackgroundColor{};
    float indicatorWidth = 10.0F;
    float indicatorHeight = 6.0F;
    float indicatorInset = 10.0F;

    auto operator<=>(const UIDropdownPaint&) const = default;
};

enum class UIDropdownCommand : u8 {
    PreviousItem,
    NextItem,
    Dismiss,
    ExitPrevious,
    ExitNext,
};

struct UIDropdownCommandResult final {
    bool consumed = false;
    bool changed = false;
    UINodeId focus{};
};

} // namespace Tina::UI
