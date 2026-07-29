#include "UIThemeBindingResolver.hpp"

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] bool scrollBarMetricsDiffer(const UIScrollViewPaint& left,
                                          const UIScrollViewPaint& right) noexcept
{
    return left.thickness != right.thickness ||
           left.minThumbExtent != right.minThumbExtent;
}

} // namespace

UIThemeNodeChrome resolveProductThemeChrome(UIWidgetKind kind,
                                            const UITheme& theme) noexcept
{
    UIThemeNodeChrome resolved{};
    switch (kind)
    {
    case UIWidgetKind::Root:
    case UIWidgetKind::Panel:
        break;
    case UIWidgetKind::Modal:
        resolved.bindings = ThemeBindingBoxPaint;
        resolved.boxPaint = makePanelBoxPaint(
            theme, scaleColorAlpha(theme.surface1, 248), UIElevation::Low);
        break;
    case UIWidgetKind::ScrollView:
        resolved.bindings = ThemeBindingScrollViewPaint;
        resolved.scrollViewPaint = makeScrollViewPaint(theme);
        break;
    case UIWidgetKind::ListView:
        resolved.bindings = ThemeBindingBoxPaint | ThemeBindingListViewPaint;
        resolved.boxPaint =
            makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 245));
        resolved.listViewPaint = makeListViewPaint(theme);
        break;
    case UIWidgetKind::TreeView:
        resolved.bindings = ThemeBindingBoxPaint | ThemeBindingTreeViewPaint;
        resolved.boxPaint =
            makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 245));
        resolved.treeViewPaint = makeTreeViewPaint(theme);
        break;
    case UIWidgetKind::Popup:
        resolved.bindings = ThemeBindingBoxPaint;
        resolved.boxPaint = makePopupBoxPaint(theme);
        break;
    case UIWidgetKind::Label:
        resolved.bindings = ThemeBindingTextStyle;
        resolved.textStyle = makeBodyTextStyle(theme);
        break;
    case UIWidgetKind::Button: {
        const UIButtonChrome chrome = makeButtonChrome(theme);
        resolved.bindings = ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                            ThemeBindingTextStyle;
        resolved.boxPaint = chrome.box;
        resolved.buttonPaint = chrome.states;
        resolved.textStyle = chrome.label;
        break;
    }
    case UIWidgetKind::Dropdown: {
        const UIDropdownChrome chrome = makeDropdownChrome(theme);
        resolved.bindings = ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                            ThemeBindingTextStyle | ThemeBindingDropdownPaint;
        resolved.boxPaint = chrome.box;
        resolved.buttonPaint = chrome.states;
        resolved.textStyle = chrome.label;
        resolved.dropdownPaint = chrome.dropdown;
        break;
    }
    case UIWidgetKind::DropdownItem:
    case UIWidgetKind::ListViewItem:
    case UIWidgetKind::TreeViewItem: {
        const UIButtonChrome chrome = makeDropdownItemChrome(theme);
        resolved.bindings = ThemeBindingBoxPaint | ThemeBindingButtonPaint |
                            ThemeBindingTextStyle;
        resolved.boxPaint = chrome.box;
        resolved.buttonPaint = chrome.states;
        resolved.textStyle = chrome.label;
        break;
    }
    case UIWidgetKind::Checkbox: {
        const UICheckboxChrome chrome = makeCheckboxChrome(theme);
        resolved.bindings = ThemeBindingBoxPaint | ThemeBindingCheckboxPaint;
        resolved.boxPaint = chrome.box;
        resolved.checkboxPaint = chrome.indicator;
        break;
    }
    case UIWidgetKind::Slider: {
        const UISliderChrome chrome = makeSliderChrome(theme);
        resolved.bindings = ThemeBindingBoxPaint | ThemeBindingSliderPaint;
        resolved.boxPaint = chrome.track;
        resolved.sliderPaint = chrome.slider;
        break;
    }
    case UIWidgetKind::TextEdit: {
        const UITextEditChrome chrome = makeTextEditChrome(theme);
        resolved.bindings = ThemeBindingBoxPaint | ThemeBindingTextStyle;
        resolved.boxPaint = chrome.box;
        resolved.textStyle = chrome.text;
        break;
    }
    case UIWidgetKind::ProgressBar: {
        const UIProgressBarChrome chrome = makeProgressBarChrome(theme);
        resolved.bindings = ThemeBindingBoxPaint | ThemeBindingProgressBarPaint;
        resolved.boxPaint = chrome.track;
        resolved.progressBarPaint = chrome.bar;
        break;
    }
    case UIWidgetKind::RadioButton: {
        const UIRadioButtonChrome chrome = makeRadioButtonChrome(theme);
        resolved.bindings = ThemeBindingRadioButtonPaint |
                            ThemeBindingTextStyle;
        resolved.radioButtonPaint = chrome.radio;
        resolved.textStyle = chrome.label;
        break;
    }
    }
    return resolved;
}

UIThemeChromeChanges classifyBoundThemeChromeChanges(
    UIThemeBindingMask bindings, const UIThemeNodeChrome& current,
    const UIThemeNodeChrome& next) noexcept
{
    UIThemeChromeChanges changes{};
    const auto recordPaintChange = [&changes](bool changed) noexcept {
        changes.paint = changes.paint || changed;
    };

    recordPaintChange(hasThemeBinding(bindings, ThemeBindingBoxPaint) &&
                      current.boxPaint != next.boxPaint);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingTextStyle) &&
                      current.textStyle != next.textStyle);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingButtonPaint) &&
                      current.buttonPaint != next.buttonPaint);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingCheckboxPaint) &&
                      current.checkboxPaint != next.checkboxPaint);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingSliderPaint) &&
                      current.sliderPaint != next.sliderPaint);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingProgressBarPaint) &&
                      current.progressBarPaint != next.progressBarPaint);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingRadioButtonPaint) &&
                      current.radioButtonPaint != next.radioButtonPaint);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingScrollViewPaint) &&
                      current.scrollViewPaint != next.scrollViewPaint);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingDropdownPaint) &&
                      current.dropdownPaint != next.dropdownPaint);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingListViewPaint) &&
                      current.listViewPaint != next.listViewPaint);
    recordPaintChange(hasThemeBinding(bindings, ThemeBindingTreeViewPaint) &&
                      current.treeViewPaint != next.treeViewPaint);

    if (hasThemeBinding(bindings, ThemeBindingScrollViewPaint))
    {
        changes.layout = changes.layout || scrollBarMetricsDiffer(
                                               current.scrollViewPaint,
                                               next.scrollViewPaint);
    }
    if (hasThemeBinding(bindings, ThemeBindingListViewPaint))
    {
        changes.layout = changes.layout || scrollBarMetricsDiffer(
                                               current.listViewPaint.scrollBar,
                                               next.listViewPaint.scrollBar);
    }
    if (hasThemeBinding(bindings, ThemeBindingTreeViewPaint))
    {
        changes.layout = changes.layout || scrollBarMetricsDiffer(
                                               current.treeViewPaint.scrollBar,
                                               next.treeViewPaint.scrollBar);
    }
    if (hasThemeBinding(bindings, ThemeBindingDropdownPaint))
    {
        changes.layout =
            changes.layout ||
            current.dropdownPaint.indicatorWidth != next.dropdownPaint.indicatorWidth ||
            current.dropdownPaint.indicatorHeight != next.dropdownPaint.indicatorHeight ||
            current.dropdownPaint.indicatorInset != next.dropdownPaint.indicatorInset;
    }
    if (hasThemeBinding(bindings, ThemeBindingRadioButtonPaint))
    {
        changes.layout = changes.layout ||
                         current.radioButtonPaint.labelGap !=
                             next.radioButtonPaint.labelGap;
    }
    return changes;
}

} // namespace Tina::UI::Detail
