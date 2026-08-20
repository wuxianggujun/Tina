#pragma once

#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

// Checkbox and Switch deliberately share one Toggle state machine. This
// discriminator changes only committed chrome geometry.
enum class UIToggleIndicatorPresentation : u8 {
    Checkbox = 0,
    Switch,
};

// Checkbox indicator and interaction-state overrides. The node's UIBoxPaint
// remains the normal outer indicator/background, while checkedIndicatorColor
// controls the checked-state foreground quad. A zero-alpha state color falls
// back to the next state, with pressed > hovered > focused > normal precedence.
// Switch presentation uses the same fields for its unchecked track states and
// adds checked-track variants plus an always-visible sliding thumb. Disabled
// state keeps the shared widget-opacity contract.
struct UICheckboxPaint final {
    UIStraightSrgba8Color checkedIndicatorColor{};
    float checkedIndicatorInset = 6.0F;
    UIStraightSrgba8Color hoveredIndicatorColor{};
    UIStraightSrgba8Color focusedIndicatorColor{};
    UIStraightSrgba8Color pressedIndicatorColor{};
    UIStraightSrgba8Color uncheckedIndicatorColor{};
    UIStraightSrgba8Color checkedBackgroundColor{};
    UIStraightSrgba8Color checkedHoveredBackgroundColor{};
    UIStraightSrgba8Color checkedFocusedBackgroundColor{};
    UIStraightSrgba8Color checkedPressedBackgroundColor{};
    UIToggleIndicatorPresentation presentation = UIToggleIndicatorPresentation::Checkbox;

    auto operator<=>(const UICheckboxPaint&) const = default;
};

} // namespace Tina::UI
