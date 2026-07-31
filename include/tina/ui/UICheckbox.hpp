#pragma once

#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

// Checkbox indicator and interaction-state overrides. The node's UIBoxPaint
// remains the normal outer indicator/background, while checkedIndicatorColor
// controls the checked-state foreground quad. A zero-alpha state color falls
// back to the next state, with pressed > hovered > focused > normal precedence.
// Disabled state keeps the shared widget-opacity contract.
struct UICheckboxPaint final {
    UIStraightSrgba8Color checkedIndicatorColor{};
    float checkedIndicatorInset = 6.0F;
    UIStraightSrgba8Color hoveredIndicatorColor{};
    UIStraightSrgba8Color focusedIndicatorColor{};
    UIStraightSrgba8Color pressedIndicatorColor{};

    auto operator<=>(const UICheckboxPaint&) const = default;
};

} // namespace Tina::UI
