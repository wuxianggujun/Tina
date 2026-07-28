#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/ui/UILayout.hpp>

#include <compare>

namespace Tina::UI {

enum class UIPopupPlacement : u8 {
    Auto,
    Below,
    Above,
};

struct UIPopupStyle final {
    UIPopupPlacement placement = UIPopupPlacement::Auto;
    float anchorGap = 4.0F;
    bool matchAnchorWidth = true;

    auto operator<=>(const UIPopupStyle&) const = default;
};

// Geometry from the last successful layout publication. Live open state is
// queried separately because a failed commit must preserve these metrics.
struct UIPopupMetrics final {
    UILogicalRect anchorRect{};
    UILogicalRect popupRect{};
    UIPopupPlacement resolvedPlacement = UIPopupPlacement::Below;
    bool open = false;

    auto operator<=>(const UIPopupMetrics&) const = default;
};

} // namespace Tina::UI
