#include <tina/ui/UITheme.hpp>

// Header isolation: UITheme must compile without third-party tokens.
namespace {

[[maybe_unused]] constexpr Tina::UI::UITheme kTheme = Tina::UI::makeModernDesktopTheme();
[[maybe_unused]] constexpr Tina::UI::UIBoxPaint kPanel =
    Tina::UI::makePanelBoxPaint(kTheme, kTheme.colors.surface, Tina::UI::UIElevation::Raised);

// The named ramp and every level factory stay constexpr, and the default ramp
// pins the sizes the control chrome factories resolve to.
static_assert(kTheme.typography.title == 20.0F);
static_assert(kTheme.typography.body == 15.0F);
static_assert(kTheme.typography.control == 14.0F);
static_assert(Tina::UI::makeTitleTextStyle(kTheme).logicalSize == 20.0F);
static_assert(Tina::UI::makeSectionTextStyle(kTheme).logicalSize == 16.0F);
static_assert(Tina::UI::makeCaptionTextStyle(kTheme).logicalSize == 12.0F);
static_assert(Tina::UI::makeDisplayTextStyle(kTheme).logicalSize == 24.0F);

[[maybe_unused]] constexpr Tina::UI::UITypographyScale kCompact =
    Tina::UI::makeCompactTypographyScale();
static_assert(kCompact.title == 20.0F);
static_assert(kCompact.section == 16.0F);
static_assert(kCompact.body == 15.0F);
static_assert(kCompact.control == 14.0F);
static_assert(kCompact.caption == 12.0F);

} // namespace
