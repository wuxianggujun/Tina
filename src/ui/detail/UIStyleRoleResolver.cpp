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
        chrome.box = makePanelBoxPaint(theme, theme.colors.surface);
        chrome.box.borderWidth = 0.0F;
        break;
    case UIStyleRoleId::PanelElevated:
        chrome.box = makePanelBoxPaint(theme, theme.colors.surfaceContainerLow, UIElevation::Raised);
        break;
    case UIStyleRoleId::ModalSurface:
        chrome.box = makePanelBoxPaint(theme, scaleColorAlpha(theme.colors.surfaceContainerHigh, 248), UIElevation::Modal);
        break;
    case UIStyleRoleId::ModalScrim:
        chrome.box.solidFill = UISolidFill{.color = theme.colors.scrim};
        break;
    case UIStyleRoleId::PopupSurface:
    case UIStyleRoleId::MenuSurface:
        chrome.box = makePopupBoxPaint(theme);
        break;
    case UIStyleRoleId::FloatingSurface:
        chrome.box = makePanelBoxPaint(
            theme, scaleColorAlpha(theme.colors.surfaceContainerHigh, 252),
            UIElevation::Floating);
        break;
    case UIStyleRoleId::TooltipSurface:
        chrome.box = makePopupBoxPaint(theme);
        chrome.text = makeSecondaryTextStyle(theme);
        break;
    case UIStyleRoleId::DividerSubtle:
        chrome.box = makeDividerChrome(theme, UIDividerTone::Subtle).line;
        break;
    case UIStyleRoleId::DividerStrong:
        chrome.box = makeDividerChrome(theme, UIDividerTone::Strong).line;
        break;
    case UIStyleRoleId::DividerAccent:
        chrome.box = makeDividerChrome(theme, UIDividerTone::Accent).line;
        break;
    case UIStyleRoleId::BadgeNeutral: {
        const UIBadgeChrome badge = makeBadgeChrome(theme, UIBadgeTone::Neutral);
        chrome.box = badge.box;
        chrome.text = badge.label;
        break;
    }
    case UIStyleRoleId::BadgeAccent: {
        const UIBadgeChrome badge = makeBadgeChrome(theme, UIBadgeTone::Accent);
        chrome.box = badge.box;
        chrome.text = badge.label;
        break;
    }
    case UIStyleRoleId::BadgeDanger: {
        const UIBadgeChrome badge = makeBadgeChrome(theme, UIBadgeTone::Danger);
        chrome.box = badge.box;
        chrome.text = badge.label;
        break;
    }
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
    case UIStyleRoleId::TextError:
        chrome.text = makeErrorTextStyle(theme);
        break;
    case UIStyleRoleId::ButtonPrimary:
    case UIStyleRoleId::ButtonDanger: {
        const UIButtonChrome button =
            makeButtonChrome(theme, role == UIStyleRoleId::ButtonDanger ? theme.colors.error : UIStraightSrgba8Color{});
        chrome.box = button.box;
        chrome.button = button.states;
        chrome.text = button.label;
        chrome.imageTint = role == UIStyleRoleId::ButtonDanger
                               ? theme.colors.onError
                               : theme.colors.onPrimary;
        break;
    }
    case UIStyleRoleId::ButtonTonal: {
        const UIButtonChrome button = makeTonalButtonChrome(theme);
        chrome.box = button.box;
        chrome.button = button.states;
        chrome.text = button.label;
        chrome.imageTint = theme.colors.onSurface;
        break;
    }
    case UIStyleRoleId::ButtonOutlined: {
        const UIButtonChrome button = makeOutlinedButtonChrome(theme);
        chrome.box = button.box;
        chrome.button = button.states;
        chrome.text = button.label;
        chrome.imageTint = theme.colors.onSurface;
        break;
    }
    case UIStyleRoleId::ButtonText: {
        const UIButtonChrome button = makeTextButtonChrome(theme);
        chrome.box = button.box;
        chrome.button = button.states;
        chrome.text = button.label;
        chrome.imageTint = theme.colors.onSurfaceVariant;
        break;
    }
    case UIStyleRoleId::Checkbox: {
        const UICheckboxChrome checkbox = makeCheckboxChrome(theme);
        chrome.box = checkbox.box;
        chrome.checkbox = checkbox.indicator;
        break;
    }
    case UIStyleRoleId::Switch: {
        const UICheckboxChrome switchChrome = makeSwitchChrome(theme);
        chrome.box = switchChrome.box;
        chrome.checkbox = switchChrome.indicator;
        break;
    }
    case UIStyleRoleId::Slider:
    case UIStyleRoleId::SliderRed:
    case UIStyleRoleId::SliderGreen:
    case UIStyleRoleId::SliderBlue:
    case UIStyleRoleId::SliderAlpha: {
        UIStraightSrgba8Color filledTrack{};
        if (role == UIStyleRoleId::SliderRed)
        {
            filledTrack = rgba8(235, 91, 91);
        }
        else if (role == UIStyleRoleId::SliderGreen)
        {
            filledTrack = rgba8(74, 176, 113);
        }
        else if (role == UIStyleRoleId::SliderBlue)
        {
            filledTrack = rgba8(77, 142, 234);
        }
        else if (role == UIStyleRoleId::SliderAlpha)
        {
            filledTrack = theme.colors.onSurfaceVariant;
        }
        const UISliderChrome slider = makeSliderChrome(theme, filledTrack);
        chrome.box = slider.hitSurface;
        chrome.slider = slider.slider;
        break;
    }
    case UIStyleRoleId::Splitter:
        chrome.splitter = makeSplitterChrome(theme).splitter;
        break;
    case UIStyleRoleId::TextInput: {
        const UITextEditChrome textInput = makeTextEditChrome(theme);
        chrome.box = textInput.box;
        chrome.textEdit = textInput.paint;
        chrome.text = textInput.text;
        break;
    }
    case UIStyleRoleId::TextInputInvalid: {
        const UITextEditChrome textInput = makeInvalidTextEditChrome(theme);
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
        chrome.imageTint = theme.colors.onSurfaceVariant;
        break;
    }
    case UIStyleRoleId::Tab: {
        const UITabChrome tab = makeTabChrome(theme);
        chrome.box = tab.box;
        chrome.tab = tab.tab;
        chrome.text = tab.label;
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
    case UIStyleRoleId::CollectionItem:
    case UIStyleRoleId::MenuItem: {
        const UIButtonChrome item = makeDropdownItemChrome(theme);
        chrome.box = item.box;
        chrome.button = item.states;
        chrome.text = item.label;
        break;
    }
    case UIStyleRoleId::ListView:
        chrome.box = makePanelBoxPaint(theme, scaleColorAlpha(theme.colors.surfaceContainerLow, 245));
        chrome.listView = makeListViewPaint(theme);
        break;
    case UIStyleRoleId::TreeView:
        chrome.box = makePanelBoxPaint(theme, scaleColorAlpha(theme.colors.surfaceContainerLow, 245));
        chrome.treeView = makeTreeViewPaint(theme);
        break;
    case UIStyleRoleId::VirtualGridView:
        chrome.box = makePanelBoxPaint(theme, scaleColorAlpha(theme.colors.surfaceContainerLow, 245));
        chrome.virtualGridView = makeVirtualGridViewPaint(theme);
        break;
    case UIStyleRoleId::DataGrid:
        chrome.box = makePanelBoxPaint(theme, scaleColorAlpha(theme.colors.surfaceContainerLow, 245));
        chrome.dataGrid = makeDataGridPaint(theme);
        break;
    case UIStyleRoleId::IconOnSurface:
        chrome.imageTint = theme.colors.onSurface;
        break;
    case UIStyleRoleId::IconOnPrimary:
        chrome.imageTint = theme.colors.onPrimary;
        break;
    case UIStyleRoleId::IconOnError:
        chrome.imageTint = theme.colors.onError;
        break;
    }
    return chrome;
}

} // namespace Tina::UI::Detail
