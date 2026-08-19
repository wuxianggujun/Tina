#pragma once

#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

// RadioButton indicator and interaction-state overrides. A zero-alpha state
// color falls back to the next state. Indicator chrome uses
// pressed > hovered > focused > normal precedence. Indicatorless background
// chrome uses disabled > pressed > selected > hovered > focused > base;
// disabled chrome also keeps the shared widget-opacity contract.
// RadioButtons sharing one direct parent form an exclusive group; separate
// groups should use separate parent Panels.
struct UIRadioButtonPaint final {
    UIStraightSrgba8Color indicatorColor{};
    UIStraightSrgba8Color selectedIndicatorColor{};
    float indicatorExtent = 16.0F;
    float selectedIndicatorInset = 6.0F;
    float labelGap = 8.0F;
    UIStraightSrgba8Color hoveredIndicatorColor{};
    UIStraightSrgba8Color focusedIndicatorColor{};
    UIStraightSrgba8Color pressedIndicatorColor{};
    bool indicatorVisible = true;
    UIStraightSrgba8Color selectedBackgroundColor{};
    UIStraightSrgba8Color hoveredBackgroundColor{};
    UIStraightSrgba8Color selectedHoveredBackgroundColor{};
    UIStraightSrgba8Color focusedBackgroundColor{};
    UIStraightSrgba8Color selectedFocusedBackgroundColor{};
    UIStraightSrgba8Color pressedBackgroundColor{};
    UIStraightSrgba8Color selectedPressedBackgroundColor{};
    UIStraightSrgba8Color disabledBackgroundColor{};
    UIStraightSrgba8Color focusedBorderColor{};

    auto operator<=>(const UIRadioButtonPaint&) const = default;
};

} // namespace Tina::UI
