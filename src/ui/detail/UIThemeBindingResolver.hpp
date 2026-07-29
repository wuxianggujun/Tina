#pragma once

#include <tina/ui/UITheme.hpp>
#include <tina/ui/UIWidgetKind.hpp>

namespace Tina::UI::Detail {

using UIThemeBindingMask = u16;

inline constexpr UIThemeBindingMask ThemeBindingBoxPaint = 1U << 0U;
inline constexpr UIThemeBindingMask ThemeBindingTextStyle = 1U << 1U;
inline constexpr UIThemeBindingMask ThemeBindingButtonPaint = 1U << 2U;
inline constexpr UIThemeBindingMask ThemeBindingCheckboxPaint = 1U << 3U;
inline constexpr UIThemeBindingMask ThemeBindingSliderPaint = 1U << 4U;
inline constexpr UIThemeBindingMask ThemeBindingProgressBarPaint = 1U << 5U;
inline constexpr UIThemeBindingMask ThemeBindingRadioButtonPaint = 1U << 6U;
inline constexpr UIThemeBindingMask ThemeBindingScrollViewPaint = 1U << 7U;
inline constexpr UIThemeBindingMask ThemeBindingDropdownPaint = 1U << 8U;
inline constexpr UIThemeBindingMask ThemeBindingListViewPaint = 1U << 9U;
inline constexpr UIThemeBindingMask ThemeBindingTreeViewPaint = 1U << 10U;

struct UIThemeNodeChrome final {
    UIThemeBindingMask bindings = 0;
    UIBoxPaint boxPaint{};
    UITextStyle textStyle{};
    UIButtonPaint buttonPaint{};
    UICheckboxPaint checkboxPaint{};
    UISliderPaint sliderPaint{};
    UIProgressBarPaint progressBarPaint{};
    UIRadioButtonPaint radioButtonPaint{};
    UIScrollViewPaint scrollViewPaint{};
    UIDropdownPaint dropdownPaint{};
    UIListViewPaint listViewPaint{};
    UITreeViewPaint treeViewPaint{};
};

struct UIThemeChromeChanges final {
    bool paint = false;
    bool layout = false;
};

[[nodiscard]] constexpr bool hasThemeBinding(UIThemeBindingMask bindings,
                                             UIThemeBindingMask binding) noexcept
{
    return (bindings & binding) != 0;
}

[[nodiscard]] UIThemeNodeChrome
resolveProductThemeChrome(UIWidgetKind kind, const UITheme& theme) noexcept;

// Text layout invalidation depends on measured glyph metrics and remains in
// UIContext. This classifier owns every other theme-managed chrome property.
[[nodiscard]] UIThemeChromeChanges classifyBoundThemeChromeChanges(
    UIThemeBindingMask bindings, const UIThemeNodeChrome& current,
    const UIThemeNodeChrome& next) noexcept;

} // namespace Tina::UI::Detail
