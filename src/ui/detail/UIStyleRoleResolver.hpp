#pragma once

#include <tina/ui/UITheme.hpp>

namespace Tina::UI::Detail {

inline constexpr u16 ThemeBindingBoxPaint = 1U << 0U;
inline constexpr u16 ThemeBindingTextStyle = 1U << 1U;
inline constexpr u16 ThemeBindingButtonPaint = 1U << 2U;
inline constexpr u16 ThemeBindingCheckboxPaint = 1U << 3U;
inline constexpr u16 ThemeBindingSliderPaint = 1U << 4U;
inline constexpr u16 ThemeBindingProgressBarPaint = 1U << 5U;
inline constexpr u16 ThemeBindingRadioButtonPaint = 1U << 6U;
inline constexpr u16 ThemeBindingScrollViewPaint = 1U << 7U;
inline constexpr u16 ThemeBindingDropdownPaint = 1U << 8U;
inline constexpr u16 ThemeBindingListViewPaint = 1U << 9U;
inline constexpr u16 ThemeBindingTreeViewPaint = 1U << 10U;
inline constexpr u16 ThemeBindingTextEditPaint = 1U << 11U;

struct ProductChrome final {
    UIBoxPaint box{};
    UITextStyle text{};
    UIButtonPaint button{};
    UICheckboxPaint checkbox{};
    UISliderPaint slider{};
    UIProgressBarPaint progressBar{};
    UIRadioButtonPaint radioButton{};
    UIScrollViewPaint scrollView{};
    UIDropdownPaint dropdown{};
    UIListViewPaint listView{};
    UITreeViewPaint treeView{};
    UITextEditPaint textEdit{};
};

[[nodiscard]] constexpr bool isValidStyleRole(UIStyleRoleId role) noexcept
{
    return role >= UIStyleRoleId::None && role <= UIStyleRoleId::TreeView;
}

[[nodiscard]] constexpr u16 defaultThemeBindingsFor(UIStyleRoleId role) noexcept
{
    switch (role)
    {
    case UIStyleRoleId::None:
        return 0;
    case UIStyleRoleId::PanelSurface:
    case UIStyleRoleId::PanelElevated:
    case UIStyleRoleId::ModalSurface:
    case UIStyleRoleId::PopupSurface:
        return ThemeBindingBoxPaint;
    case UIStyleRoleId::TextBody:
    case UIStyleRoleId::TextTitle:
    case UIStyleRoleId::TextSecondary:
    case UIStyleRoleId::TextAccent:
        return ThemeBindingTextStyle;
    case UIStyleRoleId::ButtonPrimary:
    case UIStyleRoleId::ButtonDanger:
    case UIStyleRoleId::CollectionItem:
        return ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle;
    case UIStyleRoleId::Checkbox:
        return ThemeBindingBoxPaint | ThemeBindingCheckboxPaint;
    case UIStyleRoleId::Slider:
        return ThemeBindingBoxPaint | ThemeBindingSliderPaint;
    case UIStyleRoleId::TextInput:
        return ThemeBindingBoxPaint | ThemeBindingTextStyle | ThemeBindingTextEditPaint;
    case UIStyleRoleId::ProgressBar:
        return ThemeBindingBoxPaint | ThemeBindingProgressBarPaint;
    case UIStyleRoleId::RadioButton:
        return ThemeBindingRadioButtonPaint | ThemeBindingTextStyle;
    case UIStyleRoleId::ScrollView:
        return ThemeBindingScrollViewPaint;
    case UIStyleRoleId::Dropdown:
        return ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle |
               ThemeBindingDropdownPaint;
    case UIStyleRoleId::ListView:
        return ThemeBindingBoxPaint | ThemeBindingListViewPaint;
    case UIStyleRoleId::TreeView:
        return ThemeBindingBoxPaint | ThemeBindingTreeViewPaint;
    }
    return 0;
}

[[nodiscard]] ProductChrome productChromeFor(UIStyleRoleId role, const UITheme& theme) noexcept;

} // namespace Tina::UI::Detail
