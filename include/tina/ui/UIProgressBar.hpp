#pragma once

#include <tina/ui/UIPaint.hpp>

#include <compare>

namespace Tina::UI {

// Horizontal determinate progress fill. The node's UIBoxPaint remains the
// track; this paint controls the value-derived foreground fill.
struct UIProgressBarPaint final {
    UIStraightSrgba8Color fillColor{};

    auto operator<=>(const UIProgressBarPaint&) const = default;
};

} // namespace Tina::UI
