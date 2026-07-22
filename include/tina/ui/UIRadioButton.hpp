#pragma once

#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

// Selected RadioButton indicator. RadioButtons sharing one direct parent form
// an exclusive group; separate groups should use separate parent Panels.
struct UIRadioButtonPaint final {
    UIStraightSrgba8Color indicatorColor{};
    UIStraightSrgba8Color selectedIndicatorColor{};
    float selectedIndicatorInset = 6.0F;
    float labelGap = 8.0F;

    auto operator<=>(const UIRadioButtonPaint&) const = default;
};

} // namespace Tina::UI
