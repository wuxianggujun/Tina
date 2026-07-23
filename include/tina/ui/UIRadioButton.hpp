#pragma once

#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

// RadioButton indicator and interaction-state overrides. A zero-alpha state
// color falls back to indicatorColor; pressed takes precedence over focused.
// Disabled state keeps the shared widget-opacity contract. RadioButtons
// sharing one direct parent form an exclusive group; separate groups should
// use separate parent Panels.
struct UIRadioButtonPaint final {
    UIStraightSrgba8Color indicatorColor{};
    UIStraightSrgba8Color selectedIndicatorColor{};
    float selectedIndicatorInset = 6.0F;
    float labelGap = 8.0F;
    UIStraightSrgba8Color focusedIndicatorColor{};
    UIStraightSrgba8Color pressedIndicatorColor{};

    auto operator<=>(const UIRadioButtonPaint&) const = default;
};

} // namespace Tina::UI
