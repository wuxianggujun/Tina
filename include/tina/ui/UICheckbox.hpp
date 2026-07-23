#pragma once

#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

// Checked Checkbox indicator. The node's UIBoxPaint remains the unchecked
// track/background; this paint controls the checked-state foreground quad.
struct UICheckboxPaint final {
    UIStraightSrgba8Color checkedIndicatorColor{};
    float checkedIndicatorInset = 6.0F;

    auto operator<=>(const UICheckboxPaint&) const = default;
};

} // namespace Tina::UI
