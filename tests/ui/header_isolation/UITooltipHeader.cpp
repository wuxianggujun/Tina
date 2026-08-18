#include <tina/ui/UITooltip.hpp>

namespace {

constexpr Tina::UI::UITooltipConfig Tooltip{};
static_assert(Tooltip.placement == Tina::UI::UITooltipPlacement::Auto);
static_assert(Tina::UI::hasTooltipTrigger(
    Tooltip.triggers, Tina::UI::UITooltipTrigger::PointerHover));

} // namespace
