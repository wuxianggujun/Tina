#pragma once

#include <tina/ui/UIButton.hpp>
#include <tina/ui/UICheckbox.hpp>
#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIProgressBar.hpp>
#include <tina/ui/UIRadioButton.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UISlider.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UITabView.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UITreeView.hpp>

#include <compare>
#include <optional>

namespace Tina::UI {

// Named desktop typography ramp in logical px. One ramp per Theme keeps font
// sizes out of call sites and lets a product swap the whole scale in one
// assignment. Every level must be finite and positive (validateProductTheme).
struct UITypographyScale final {
    float display = 28.0F;
    float title = 22.0F;
    float section = 20.0F;
    float body = 18.0F;
    float control = 18.0F;
    float caption = 14.0F;

    auto operator<=>(const UITypographyScale&) const = default;
};

// Tina Studio Compact: the tighter desktop ramp used by Editor-style tool
// surfaces where vertical density matters more than reading comfort.
[[nodiscard]] constexpr UITypographyScale makeCompactTypographyScale() noexcept
{
    return UITypographyScale{
        .display = 24.0F,
        .title = 20.0F,
        .section = 16.0F,
        .body = 15.0F,
        .control = 14.0F,
        .caption = 14.0F,
    };
}

// Minimal visual elevation for Phase B fake shadow.
enum class UIElevation : u8 {
    None = 0,
    Low = 1,
};

// Product Theme tokens + control chrome factories. Not a CSS resolver.
// UIContext owns the active theme and can transactionally re-theme existing
// default chrome. Local setBoxPaint / set*Paint / setTextStyle calls detach only
// the corresponding property from later theme updates.
struct UITheme final {
    UIStraightSrgba8Color surface0 = rgb(0x101317);
    UIStraightSrgba8Color surface1 = rgb(0x171B21);
    UIStraightSrgba8Color surface2 = rgb(0x222831);
    UIStraightSrgba8Color borderLight = rgb(0x3B4653, 210);
    UIStraightSrgba8Color borderDark = rgb(0x3B4653, 210);
    UIStraightSrgba8Color textPrimary = rgb(0xE7ECF2);
    UIStraightSrgba8Color textSecondary = rgb(0xAAB4C0);
    UIStraightSrgba8Color textTitle = rgb(0xF2F5F8);
    UIStraightSrgba8Color textAccent = rgb(0xF4C95D);
    UIStraightSrgba8Color accent = rgb(0x4C9AFF);
    UIStraightSrgba8Color onAccent = rgb(0xFFFFFF);
    UIStraightSrgba8Color danger = rgb(0xD9555F);
    UIStraightSrgba8Color focusRing = rgb(0x76B7FF, 245);
    UIStraightSrgba8Color shadow = rgb(0x000000, 0);
    UIStraightSrgba8Color buttonNormal = rgb(0x2F78B7);
    UIStraightSrgba8Color buttonDisabled = rgb(0x343B44, 230);
    UIStraightSrgba8Color scrollBarTrack = rgb(0x11161C, 210);
    UIStraightSrgba8Color scrollBarThumb = rgb(0x596776, 235);
    UIStraightSrgba8Color scrollBarThumbActive = rgb(0x76B7FF, 250);

    float panelBorderWidth = 1.0F;
    float panelCornerRadius = 6.0F;
    float controlCornerRadius = 4.0F;
    float panelShadowOffsetX = 0.0F;
    float panelShadowOffsetY = 0.0F;
    float checkboxIndicatorInset = 6.0F;
    float radioSelectedInset = 6.0F;
    float radioLabelGap = 8.0F;
    float sliderContentInset = 4.0F;
    float sliderThumbWidth = 8.0F;
    UITypographyScale typography{};

    auto operator<=>(const UITheme&) const = default;
};

[[nodiscard]] constexpr UITheme makeDefaultProductTheme() noexcept
{
    return UITheme{};
}

[[nodiscard]] constexpr UITheme makeLightProductTheme() noexcept
{
    UITheme theme{};
    theme.surface0 = rgb(0xF2F4F8);
    theme.surface1 = rgb(0xFFFFFF);
    theme.surface2 = rgb(0xE8EDF3);
    theme.borderLight = rgb(0xB8C2CD, 220);
    theme.borderDark = rgb(0xB8C2CD, 220);
    theme.textPrimary = rgb(0x20262D);
    theme.textSecondary = rgb(0x5C6773);
    theme.textTitle = rgb(0x171C22);
    theme.textAccent = rgb(0x956600);
    theme.accent = rgb(0x1769AA);
    theme.onAccent = rgb(0xFFFFFF);
    theme.danger = rgb(0xBA1A1A);
    theme.focusRing = rgb(0x1769AA, 245);
    theme.shadow = rgb(0x000000, 0);
    theme.buttonNormal = rgb(0x1769AA);
    theme.buttonDisabled = rgb(0xC8CED5, 230);
    theme.scrollBarTrack = rgb(0xD8DEE5, 230);
    theme.scrollBarThumb = rgb(0x748291, 235);
    theme.scrollBarThumbActive = rgb(0x1769AA, 250);
    return theme;
}

[[nodiscard]] constexpr UIStraightSrgba8Color scaleColorAlpha(
    UIStraightSrgba8Color color,
    u8 alpha) noexcept
{
    color.alpha = alpha;
    return color;
}

[[nodiscard]] constexpr UIStraightSrgba8Color darkenChannel(
    UIStraightSrgba8Color color,
    u8 amount) noexcept
{
    const auto darken = [amount](u8 channel) noexcept -> u8 {
        return channel > amount ? static_cast<u8>(channel - amount) : u8{0};
    };
    return rgba8(darken(color.red), darken(color.green), darken(color.blue), color.alpha);
}

[[nodiscard]] constexpr UIStraightSrgba8Color lightenChannel(
    UIStraightSrgba8Color color,
    u8 amount) noexcept
{
    const auto lighten = [amount](u8 channel) noexcept -> u8 {
        const u16 sum = static_cast<u16>(channel) + amount;
        return sum > 255U ? u8{255} : static_cast<u8>(sum);
    };
    return rgba8(lighten(color.red), lighten(color.green), lighten(color.blue), color.alpha);
}

// Product panel chrome: fill + border + optional shadow + rounded outer shape.
[[nodiscard]] constexpr UIBoxPaint makePanelBoxPaint(
    const UITheme& theme,
    UIStraightSrgba8Color fill,
    UIElevation elevation = UIElevation::None) noexcept
{
    UIBoxPaint paint{
        .solidFill = UISolidFill{.color = fill},
        .borderLight = theme.borderLight,
        .borderDark = theme.borderDark,
        .borderWidth = theme.panelBorderWidth,
    };
    paint.cornerRadii = UILogicalCornerRadii::uniform(theme.panelCornerRadius);
    if (elevation == UIElevation::Low) {
        paint.shadow = theme.shadow;
        paint.shadowOffsetX = theme.panelShadowOffsetX;
        paint.shadowOffsetY = theme.panelShadowOffsetY;
    }
    return paint;
}

[[nodiscard]] constexpr UITextStyle makeTitleTextStyle(
    const UITheme& theme,
    float logicalSize = 0.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.typography.title,
        .advanceScale = 0.65F,
        .lineHeightScale = 1.15F,
        .color = theme.textTitle,
    };
}

[[nodiscard]] constexpr UITextStyle makeBodyTextStyle(
    const UITheme& theme,
    float logicalSize = 0.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.typography.body,
        .advanceScale = 0.62F,
        .lineHeightScale = 1.12F,
        .color = theme.textPrimary,
    };
}

[[nodiscard]] constexpr UITextStyle makeSecondaryTextStyle(
    const UITheme& theme,
    float logicalSize = 0.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.typography.body,
        .advanceScale = 0.62F,
        .lineHeightScale = 1.12F,
        .color = theme.textSecondary,
    };
}

[[nodiscard]] constexpr UITextStyle makeAccentTextStyle(
    const UITheme& theme,
    float logicalSize = 0.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.typography.title,
        .advanceScale = 0.65F,
        .lineHeightScale = 1.15F,
        .color = theme.textAccent,
    };
}

// Largest ramp level. Reserved for hero/empty-state surfaces; no control chrome
// uses it by default.
[[nodiscard]] constexpr UITextStyle makeDisplayTextStyle(
    const UITheme& theme,
    float logicalSize = 0.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.typography.display,
        .advanceScale = 0.65F,
        .lineHeightScale = 1.15F,
        .color = theme.textTitle,
    };
}

// Group/section headers between title and body. Shares the title family's
// advance/line-height so a section header only differs from a title by size.
[[nodiscard]] constexpr UITextStyle makeSectionTextStyle(
    const UITheme& theme,
    float logicalSize = 0.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.typography.section,
        .advanceScale = 0.65F,
        .lineHeightScale = 1.15F,
        .color = theme.textTitle,
    };
}

// Dense metadata rows: paths, hints, counters. Uses the secondary text color so
// captions stay subordinate to body text without a per-call override.
[[nodiscard]] constexpr UITextStyle makeCaptionTextStyle(
    const UITheme& theme,
    float logicalSize = 0.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.typography.caption,
        .advanceScale = 0.62F,
        .lineHeightScale = 1.12F,
        .color = theme.textSecondary,
    };
}

struct UIButtonChrome final {
    UIBoxPaint box{};
    UIButtonPaint states{};
    UITextStyle label{};
};

[[nodiscard]] constexpr UIButtonChrome makeButtonChrome(
    const UITheme& theme,
    UIStraightSrgba8Color normalFill = {}) noexcept
{
    const UIStraightSrgba8Color fill =
        normalFill.alpha != 0 ? normalFill : scaleColorAlpha(theme.buttonNormal, 230);
    UIBoxPaint box = makePanelBoxPaint(theme, fill, UIElevation::None);
    box.cornerRadii = UILogicalCornerRadii::uniform(theme.controlCornerRadius);
    box.borderWidth = 0.0F;
    return UIButtonChrome{
        .box = box,
        .states =
            UIButtonPaint{
                .hoveredBackgroundColor = scaleColorAlpha(lightenChannel(fill, 28), 240),
                .pressedBackgroundColor = darkenChannel(fill, 36),
                .focusedBackgroundColor = lightenChannel(fill, 12),
                .disabledBackgroundColor = theme.buttonDisabled,
                .focusedBorderColor = theme.focusRing,
            },
        .label =
            UITextStyle{
                .logicalSize = theme.typography.control,
                .advanceScale = 0.62F,
                .lineHeightScale = 1.15F,
                .color = theme.onAccent,
            },
    };
}

[[nodiscard]] constexpr UIButtonChrome makeTonalButtonChrome(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color fill = scaleColorAlpha(theme.surface2, 245);
    UIBoxPaint box = makePanelBoxPaint(theme, fill, UIElevation::None);
    box.cornerRadii = UILogicalCornerRadii::uniform(theme.controlCornerRadius);
    box.borderWidth = 0.0F;
    return UIButtonChrome{
        .box = box,
        .states =
            UIButtonPaint{
                .hoveredBackgroundColor = lightenChannel(fill, 14),
                .pressedBackgroundColor = darkenChannel(fill, 20),
                .focusedBackgroundColor = lightenChannel(fill, 8),
                .disabledBackgroundColor = theme.buttonDisabled,
                .focusedBorderColor = theme.focusRing,
            },
        .label = makeBodyTextStyle(theme, theme.typography.control),
    };
}

[[nodiscard]] constexpr UIButtonChrome makeOutlinedButtonChrome(const UITheme& theme) noexcept
{
    UIButtonChrome chrome = makeTonalButtonChrome(theme);
    chrome.box.solidFill = UISolidFill{.color = scaleColorAlpha(theme.surface1, 32)};
    chrome.box.borderLight = theme.borderLight;
    chrome.box.borderDark = theme.borderDark;
    chrome.box.borderWidth = theme.panelBorderWidth;
    chrome.states.hoveredBackgroundColor = scaleColorAlpha(theme.surface2, 220);
    chrome.states.focusedBackgroundColor = scaleColorAlpha(theme.surface2, 180);
    chrome.states.pressedBackgroundColor = scaleColorAlpha(theme.accent, 90);
    return chrome;
}

[[nodiscard]] constexpr UIButtonChrome makeTextButtonChrome(const UITheme& theme) noexcept
{
    UIButtonChrome chrome = makeTonalButtonChrome(theme);
    chrome.box = makeSolidBox(scaleColorAlpha(theme.surface1, 1), theme.controlCornerRadius);
    chrome.states.hoveredBackgroundColor = scaleColorAlpha(theme.surface2, 210);
    chrome.states.focusedBackgroundColor = scaleColorAlpha(theme.surface2, 170);
    chrome.states.pressedBackgroundColor = scaleColorAlpha(theme.accent, 80);
    chrome.label.color = theme.textSecondary;
    return chrome;
}

struct UICheckboxChrome final {
    UIBoxPaint box{};
    UICheckboxPaint indicator{};
};

[[nodiscard]] constexpr UICheckboxChrome makeCheckboxChrome(
    const UITheme& theme,
    UIStraightSrgba8Color boxFill = {}) noexcept
{
    const UIStraightSrgba8Color fill =
        boxFill.alpha != 0 ? boxFill : scaleColorAlpha(theme.surface2, 230);
    return UICheckboxChrome{
        .box = makeSolidBox(fill, theme.controlCornerRadius),
        .indicator =
            UICheckboxPaint{
                .checkedIndicatorColor = theme.textPrimary,
                .checkedIndicatorInset = theme.checkboxIndicatorInset,
                .hoveredIndicatorColor = lightenChannel(fill, 28),
                .focusedIndicatorColor = theme.focusRing,
                .pressedIndicatorColor = darkenChannel(fill, 36),
            },
    };
}

struct UISliderChrome final {
    UIBoxPaint track{};
    UISliderPaint slider{};
};

[[nodiscard]] constexpr UISliderChrome makeSliderChrome(
    const UITheme& theme,
    UIStraightSrgba8Color filledTrack = {}) noexcept
{
    const UIStraightSrgba8Color filled =
        filledTrack.alpha != 0 ? filledTrack : theme.accent;
    return UISliderChrome{
        .track = makeSolidBox(scaleColorAlpha(theme.surface2, 230)),
        .slider =
            UISliderPaint{
                .filledTrackColor = filled,
                .thumbColor = theme.textPrimary,
                .draggingThumbColor = theme.textAccent,
                .focusedThumbColor = theme.focusRing,
                .contentInset = theme.sliderContentInset,
                .thumbWidth = theme.sliderThumbWidth,
            },
    };
}

struct UIProgressBarChrome final {
    UIBoxPaint track{};
    UIProgressBarPaint bar{};
};

[[nodiscard]] constexpr UIProgressBarChrome makeProgressBarChrome(const UITheme& theme) noexcept
{
    return UIProgressBarChrome{
        .track = makeSolidBox(scaleColorAlpha(theme.surface2, 240)),
        .bar = UIProgressBarPaint{.fillColor = theme.accent},
    };
}

struct UIRadioButtonChrome final {
    UIRadioButtonPaint radio{};
    UITextStyle label{};
};

struct UISegmentedButtonChrome final {
    UIBoxPaint box{};
    UIRadioButtonPaint radio{};
    UITextStyle label{};
};

struct UITabChrome final {
    UIBoxPaint box{};
    UITabPaint tab{};
    UITextStyle label{};
};

[[nodiscard]] constexpr UITabChrome makeTabChrome(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color base = scaleColorAlpha(theme.surface1, 90);
    UIBoxPaint box = makePanelBoxPaint(theme, base, UIElevation::None);
    box.cornerRadii = UILogicalCornerRadii::uniform(theme.controlCornerRadius);
    return UITabChrome{
        .box = box,
        .tab =
            UITabPaint{
                .selectedBackgroundColor = scaleColorAlpha(theme.accent, 185),
                .hoveredBackgroundColor = scaleColorAlpha(theme.surface2, 245),
                .focusedBackgroundColor = scaleColorAlpha(theme.surface2, 230),
                .pressedBackgroundColor = scaleColorAlpha(theme.accent, 125),
                .disabledBackgroundColor = theme.buttonDisabled,
                .focusedBorderColor = theme.focusRing,
            },
        .label = makeBodyTextStyle(theme, theme.typography.control),
    };
}

[[nodiscard]] constexpr UISegmentedButtonChrome makeSegmentedButtonChrome(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color base = scaleColorAlpha(theme.surface1, 90);
    UIBoxPaint box = makePanelBoxPaint(theme, base, UIElevation::None);
    box.cornerRadii = UILogicalCornerRadii::uniform(theme.controlCornerRadius);
    return UISegmentedButtonChrome{
        .box = box,
        .radio =
            UIRadioButtonPaint{
                .indicatorVisible = false,
                .selectedBackgroundColor = scaleColorAlpha(theme.accent, 185),
                .hoveredBackgroundColor = scaleColorAlpha(theme.surface2, 245),
                .focusedBackgroundColor = scaleColorAlpha(theme.surface2, 230),
                .pressedBackgroundColor = scaleColorAlpha(theme.accent, 125),
                .disabledBackgroundColor = theme.buttonDisabled,
                .focusedBorderColor = theme.focusRing,
            },
        .label = makeBodyTextStyle(theme, theme.typography.control),
    };
}

[[nodiscard]] constexpr UIRadioButtonChrome makeRadioButtonChrome(const UITheme& theme) noexcept
{
    return UIRadioButtonChrome{
        .radio =
            UIRadioButtonPaint{
                .indicatorColor = theme.surface2,
                .selectedIndicatorColor = theme.textAccent,
                .selectedIndicatorInset = theme.radioSelectedInset,
                .labelGap = theme.radioLabelGap,
                .hoveredIndicatorColor = lightenChannel(theme.surface2, 28),
                .focusedIndicatorColor = theme.focusRing,
                .pressedIndicatorColor = darkenChannel(theme.accent, 40),
            },
        .label = makeBodyTextStyle(theme),
    };
}

struct UITextEditChrome final {
    UIBoxPaint box{};
    UITextEditPaint paint{};
    UITextStyle text{};
};

[[nodiscard]] constexpr UITextEditChrome makeTextEditChrome(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color fill = scaleColorAlpha(theme.surface2, 245);
    return UITextEditChrome{
        .box = makeSolidBox(fill, theme.controlCornerRadius),
        .paint =
            UITextEditPaint{
                .hoveredBackgroundColor = lightenChannel(fill, 28),
                .pressedBackgroundColor = darkenChannel(fill, 36),
                .focusedBackgroundColor = lightenChannel(fill, 12),
                .disabledBackgroundColor = theme.buttonDisabled,
                .selectionBackgroundColor = scaleColorAlpha(theme.focusRing, 190),
                .caretColor = theme.textPrimary,
            },
        .text = makeBodyTextStyle(theme, 22.0F),
    };
}

struct UISettingsPanelChrome final {
    UIBoxPaint panel{};
};

[[nodiscard]] constexpr UIScrollViewPaint makeScrollViewPaint(const UITheme& theme) noexcept
{
    return UIScrollViewPaint{
        .trackColor = theme.scrollBarTrack,
        .thumbColor = theme.scrollBarThumb,
        .draggingThumbColor = theme.scrollBarThumbActive,
        .thickness = 10.0F,
        .minThumbExtent = 24.0F,
    };
}

[[nodiscard]] constexpr UIListViewPaint makeListViewPaint(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color selected = scaleColorAlpha(theme.accent, 150);
    return UIListViewPaint{
        .scrollBar = makeScrollViewPaint(theme),
        .selectedItemBackgroundColor = selected,
        .hoveredSelectedItemBackgroundColor = lightenChannel(selected, 28),
        .focusedSelectedItemBackgroundColor = scaleColorAlpha(theme.focusRing, 180),
        .pressedSelectedItemBackgroundColor = darkenChannel(selected, 36),
    };
}

[[nodiscard]] constexpr UITreeViewPaint makeTreeViewPaint(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color selected = scaleColorAlpha(theme.accent, 150);
    return UITreeViewPaint{
        .scrollBar = makeScrollViewPaint(theme),
        .selectedItemBackgroundColor = selected,
        .hoveredSelectedItemBackgroundColor = lightenChannel(selected, 28),
        .focusedSelectedItemBackgroundColor = scaleColorAlpha(theme.focusRing, 180),
        .pressedSelectedItemBackgroundColor = darkenChannel(selected, 36),
        .disclosureColor = theme.textSecondary,
    };
}

struct UIDropdownChrome final {
    UIBoxPaint box{};
    UIButtonPaint states{};
    UITextStyle label{};
    UIDropdownPaint dropdown{};
};

[[nodiscard]] constexpr UIDropdownChrome makeDropdownChrome(const UITheme& theme) noexcept
{
    UIButtonChrome button = makeButtonChrome(theme, scaleColorAlpha(theme.surface2, 245));
    button.label.color = theme.textPrimary;
    return UIDropdownChrome{
        .box = button.box,
        .states = button.states,
        .label = button.label,
        .dropdown =
            UIDropdownPaint{
                .indicatorColor = theme.textAccent,
                .selectedItemBackgroundColor = scaleColorAlpha(theme.accent, 150),
                .indicatorWidth = 10.0F,
                .indicatorHeight = 6.0F,
                .indicatorInset = 10.0F,
            },
    };
}

[[nodiscard]] constexpr UIBoxPaint makePopupBoxPaint(const UITheme& theme) noexcept
{
    return makePanelBoxPaint(theme, scaleColorAlpha(theme.surface1, 250), UIElevation::Low);
}

[[nodiscard]] constexpr UIButtonChrome makeDropdownItemChrome(const UITheme& theme) noexcept
{
    UIButtonChrome chrome = makeButtonChrome(theme, scaleColorAlpha(theme.surface1, 245));
    chrome.box.shadow = {};
    chrome.box.shadowOffsetX = 0.0F;
    chrome.box.shadowOffsetY = 0.0F;
    chrome.label.color = theme.textPrimary;
    return chrome;
}

[[nodiscard]] constexpr UISettingsPanelChrome makeSettingsPanelChrome(const UITheme& theme) noexcept
{
    return UISettingsPanelChrome{
        .panel = makePanelBoxPaint(
            theme, scaleColorAlpha(theme.surface0, 236), UIElevation::Low),
    };
}

struct UITitlePlateChrome final {
    UIBoxPaint panel{};
    UITextStyle title{};
    UITextStyle subtitle{};
};

[[nodiscard]] constexpr UITitlePlateChrome makeTitlePlateChrome(const UITheme& theme) noexcept
{
    return UITitlePlateChrome{
        .panel = makePanelBoxPaint(
            theme, scaleColorAlpha(theme.surface1, 230), UIElevation::None),
        .title = makeTitleTextStyle(theme),
        .subtitle = makeAccentTextStyle(theme),
    };
}

} // namespace Tina::UI
