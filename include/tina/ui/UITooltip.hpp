#pragma once

#include <tina/core/base/EnumFlags.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/time/MonotonicClock.hpp>
#include <tina/ui/UILayout.hpp>

#include <compare>

namespace Tina::UI {

enum class UITooltipPlacement : u8 {
    Auto = 0,
    Above,
    Below,
    Left,
    Right,
};

enum class UITooltipTrigger : u8 {
    None = 0,
    PointerHover = 1U << 0U,
    KeyboardFocus = 1U << 1U,
    Manual = 1U << 2U,
};

TINA_ENUM_FLAG_OPERATORS(UITooltipTrigger);

[[nodiscard]] constexpr bool hasTooltipTrigger(UITooltipTrigger set,
                                               UITooltipTrigger trigger) noexcept
{
    return hasAllFlags(set, trigger);
}

struct UITooltipConfig final {
    UITooltipPlacement placement = UITooltipPlacement::Auto;
    float anchorGap = 6.0F;
    float viewportMargin = 8.0F;
    Core::Duration initialDelay{0.5};
    Core::Duration reshowDelay{0.1};
    Core::Duration dismissDelay{0.1};
    UITooltipTrigger triggers = UITooltipTrigger::PointerHover |
                                UITooltipTrigger::KeyboardFocus |
                                UITooltipTrigger::Manual;

    auto operator<=>(const UITooltipConfig&) const = default;
};

// Geometry and visibility from the last successful atomic UI publication.
// Live trigger intent is intentionally not exposed through this snapshot.
struct UITooltipMetrics final {
    UILogicalRect anchorRect{};
    UILogicalRect tooltipRect{};
    UITooltipPlacement resolvedPlacement = UITooltipPlacement::Below;
    bool open = false;

    auto operator<=>(const UITooltipMetrics&) const = default;
};

} // namespace Tina::UI
