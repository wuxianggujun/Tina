#include <tina/ui/UITheme.hpp>

// Header isolation: UITheme must compile without third-party tokens.
namespace {

[[maybe_unused]] constexpr Tina::UI::UITheme kTheme = Tina::UI::makeDefaultProductTheme();
[[maybe_unused]] constexpr Tina::UI::UIBoxPaint kPanel =
    Tina::UI::makePanelBoxPaint(kTheme, kTheme.surface1, Tina::UI::UIElevation::Low);

} // namespace
