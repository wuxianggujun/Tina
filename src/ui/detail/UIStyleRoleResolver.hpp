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
inline constexpr u16 ThemeBindingImageTint = 1U << 12U;
inline constexpr u16 ThemeBindingTabPaint = 1U << 13U;
inline constexpr u16 ThemeBindingSplitterPaint = 1U << 14U;
inline constexpr u16 ThemeBindingGridPaint = 1U << 15U;

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
    UIVirtualGridViewPaint virtualGridView{};
    UIDataGridPaint dataGrid{};
    UITextEditPaint textEdit{};
    UITabPaint tab{};
    UISplitterPaint splitter{};
    UIStraightSrgba8Color imageTint{};
};

[[nodiscard]] constexpr bool isValidStyleRole(UIStyleRoleId role) noexcept
{
    return role >= UIStyleRoleId::None && role <= UIStyleRoleId::FloatingSurface;
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
    case UIStyleRoleId::ModalScrim:
    case UIStyleRoleId::PopupSurface:
    case UIStyleRoleId::MenuSurface:
    case UIStyleRoleId::FloatingSurface:
    case UIStyleRoleId::DividerSubtle:
    case UIStyleRoleId::DividerStrong:
    case UIStyleRoleId::DividerAccent:
        return ThemeBindingBoxPaint;
    case UIStyleRoleId::TooltipSurface:
        return ThemeBindingBoxPaint | ThemeBindingTextStyle;
    case UIStyleRoleId::TextBody:
    case UIStyleRoleId::TextTitle:
    case UIStyleRoleId::TextSecondary:
    case UIStyleRoleId::TextAccent:
    case UIStyleRoleId::TextError:
        return ThemeBindingTextStyle;
    case UIStyleRoleId::ButtonPrimary:
    case UIStyleRoleId::ButtonDanger:
    case UIStyleRoleId::ButtonTonal:
    case UIStyleRoleId::ButtonOutlined:
    case UIStyleRoleId::ButtonText:
    case UIStyleRoleId::CollectionItem:
    case UIStyleRoleId::MenuItem:
        return ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle |
               ThemeBindingImageTint;
    case UIStyleRoleId::BadgeNeutral:
    case UIStyleRoleId::BadgeAccent:
    case UIStyleRoleId::BadgeDanger:
        return ThemeBindingBoxPaint | ThemeBindingTextStyle;
    case UIStyleRoleId::Checkbox:
    case UIStyleRoleId::Switch:
        return ThemeBindingBoxPaint | ThemeBindingCheckboxPaint;
    case UIStyleRoleId::Splitter:
        return ThemeBindingSplitterPaint;
    case UIStyleRoleId::Slider:
    case UIStyleRoleId::SliderRed:
    case UIStyleRoleId::SliderGreen:
    case UIStyleRoleId::SliderBlue:
    case UIStyleRoleId::SliderAlpha:
        return ThemeBindingBoxPaint | ThemeBindingSliderPaint;
    case UIStyleRoleId::TextInput:
    case UIStyleRoleId::TextInputInvalid:
        return ThemeBindingBoxPaint | ThemeBindingTextStyle | ThemeBindingTextEditPaint;
    case UIStyleRoleId::ProgressBar:
        return ThemeBindingBoxPaint | ThemeBindingProgressBarPaint;
    case UIStyleRoleId::RadioButton:
        return ThemeBindingRadioButtonPaint | ThemeBindingTextStyle;
    case UIStyleRoleId::SegmentedButton:
        return ThemeBindingBoxPaint | ThemeBindingRadioButtonPaint |
               ThemeBindingTextStyle | ThemeBindingImageTint;
    case UIStyleRoleId::Tab:
        return ThemeBindingBoxPaint | ThemeBindingTabPaint | ThemeBindingTextStyle;
    case UIStyleRoleId::ScrollView:
        return ThemeBindingScrollViewPaint;
    case UIStyleRoleId::Dropdown:
        return ThemeBindingBoxPaint | ThemeBindingButtonPaint | ThemeBindingTextStyle |
               ThemeBindingDropdownPaint;
    case UIStyleRoleId::ListView:
        return ThemeBindingBoxPaint | ThemeBindingListViewPaint;
    case UIStyleRoleId::TreeView:
        return ThemeBindingBoxPaint | ThemeBindingTreeViewPaint;
    case UIStyleRoleId::VirtualGridView:
    case UIStyleRoleId::DataGrid:
        return ThemeBindingBoxPaint | ThemeBindingGridPaint;
    case UIStyleRoleId::IconOnSurface:
    case UIStyleRoleId::IconOnPrimary:
    case UIStyleRoleId::IconOnError:
        return ThemeBindingImageTint;
    }
    return 0;
}

[[nodiscard]] ProductChrome productChromeFor(UIStyleRoleId role, const UITheme& theme) noexcept;

} // namespace Tina::UI::Detail
