#include "UIStyleRoleResolver.hpp"

namespace Tina::UI::Detail {

ProductChrome productChromeFor(UIStyleRoleId role, const UITheme& theme) noexcept
{
    ProductChrome chrome{};
    switch (role)
    {
    case UIStyleRoleId::None:
        break;
    case UIStyleRoleId::PanelSurface:
        chrome.box = makePanelBoxPaint(theme, theme.surface1);
        chrome.box.borderWidth = 0.0F;
        break;
    case UIStyleRoleId::PanelElevated:
        chrome.box = makePanelBoxPaint(theme, theme.surface1, UIElevation::Low);
        break;
    case UIStyleRoleId::ModalSurface:
        chrome.box = makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 248), UIElevation::Low);
        break;
    case UIStyleRoleId::PopupSurface:
        chrome.box = makePopupBoxPaint(theme);
        break;
    case UIStyleRoleId::TextBody:
        chrome.text = makeBodyTextStyle(theme);
        break;
    case UIStyleRoleId::TextTitle:
        chrome.text = makeTitleTextStyle(theme);
        break;
    case UIStyleRoleId::TextSecondary:
        chrome.text = makeSecondaryTextStyle(theme);
        break;
    case UIStyleRoleId::TextAccent:
        chrome.text = makeAccentTextStyle(theme);
        break;
    case UIStyleRoleId::ButtonPrimary:
    case UIStyleRoleId::ButtonDanger: {
        const UIButtonChrome button =
            makeButtonChrome(theme, role == UIStyleRoleId::ButtonDanger ? theme.danger : UIStraightSrgba8Color{});
        chrome.box = button.box;
        chrome.button = button.states;
        chrome.text = button.label;
        break;
    }
    case UIStyleRoleId::ButtonTonal: {
        const UIButtonChrome button = makeTonalButtonChrome(theme);
        chrome.box = button.box;
        chrome.button = button.states;
        chrome.text = button.label;
        break;
    }
    case UIStyleRoleId::ButtonOutlined: {
        const UIButtonChrome button = makeOutlinedButtonChrome(theme);
        chrome.box = button.box;
        chrome.button = button.states;
        chrome.text = button.label;
        break;
    }
    case UIStyleRoleId::ButtonText: {
        const UIButtonChrome button = makeTextButtonChrome(theme);
        chrome.box = button.box;
        chrome.button = button.states;
        chrome.text = button.label;
        break;
    }
    case UIStyleRoleId::Checkbox: {
        const UICheckboxChrome checkbox = makeCheckboxChrome(theme);
        chrome.box = checkbox.box;
        chrome.checkbox = checkbox.indicator;
        break;
    }
    case UIStyleRoleId::Slider: {
        const UISliderChrome slider = makeSliderChrome(theme);
        chrome.box = slider.track;
        chrome.slider = slider.slider;
        break;
    }
    case UIStyleRoleId::TextInput: {
        const UITextEditChrome textInput = makeTextEditChrome(theme);
        chrome.box = textInput.box;
        chrome.textEdit = textInput.paint;
        chrome.text = textInput.text;
        break;
    }
    case UIStyleRoleId::ProgressBar: {
        const UIProgressBarChrome progressBar = makeProgressBarChrome(theme);
        chrome.box = progressBar.track;
        chrome.progressBar = progressBar.bar;
        break;
    }
    case UIStyleRoleId::RadioButton: {
        const UIRadioButtonChrome radioButton = makeRadioButtonChrome(theme);
        chrome.radioButton = radioButton.radio;
        chrome.text = radioButton.label;
        break;
    }
    case UIStyleRoleId::SegmentedButton: {
        const UISegmentedButtonChrome segmentedButton = makeSegmentedButtonChrome(theme);
        chrome.box = segmentedButton.box;
        chrome.radioButton = segmentedButton.radio;
        chrome.text = segmentedButton.label;
        break;
    }
    case UIStyleRoleId::ScrollView:
        chrome.scrollView = makeScrollViewPaint(theme);
        break;
    case UIStyleRoleId::Dropdown: {
        const UIDropdownChrome dropdown = makeDropdownChrome(theme);
        chrome.box = dropdown.box;
        chrome.button = dropdown.states;
        chrome.text = dropdown.label;
        chrome.dropdown = dropdown.dropdown;
        break;
    }
    case UIStyleRoleId::CollectionItem: {
        const UIButtonChrome item = makeDropdownItemChrome(theme);
        chrome.box = item.box;
        chrome.button = item.states;
        chrome.text = item.label;
        break;
    }
    case UIStyleRoleId::ListView:
        chrome.box = makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 245));
        chrome.listView = makeListViewPaint(theme);
        break;
    case UIStyleRoleId::TreeView:
        chrome.box = makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 245));
        chrome.treeView = makeTreeViewPaint(theme);
        break;
    }
    return chrome;
}

} // namespace Tina::UI::Detail
