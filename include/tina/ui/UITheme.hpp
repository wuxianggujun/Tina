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
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITreeView.hpp>

#include <compare>
#include <optional>

namespace Tina::UI {

// Minimal visual elevation for Phase B fake shadow (SolidQuad only).
enum class UIElevation : u8 {
    None = 0,
    Low = 1,
};

// Product Theme tokens + control chrome factories. Not a CSS resolver.
// UIContext owns the active theme and can transactionally re-theme existing
// default chrome. Local setBoxPaint / set*Paint / setTextStyle calls detach only
// the corresponding property from later theme updates.
struct UITheme final {
    UIStraightSrgba8Color surface0 = rgb(0x0A121C);
    UIStraightSrgba8Color surface1 = rgb(0x121C28);
    UIStraightSrgba8Color surface2 = rgb(0x1A2838);
    UIStraightSrgba8Color borderLight = rgb(0x3A4E66, 200);
    UIStraightSrgba8Color borderDark = rgb(0x05080C, 220);
    UIStraightSrgba8Color textPrimary = rgb(0xEEF4F8);
    UIStraightSrgba8Color textSecondary = rgb(0xB8C6D4);
    UIStraightSrgba8Color textTitle = rgb(0x78F0FF);
    UIStraightSrgba8Color textAccent = rgb(0xFFD250);
    UIStraightSrgba8Color accent = rgb(0x2CD28E);
    UIStraightSrgba8Color danger = rgb(0xE05050);
    UIStraightSrgba8Color focusRing = rgb(0x529AD0, 245);
    UIStraightSrgba8Color shadow = rgb(0x000000, 100);
    UIStraightSrgba8Color buttonNormal = rgb(0x287850);
    UIStraightSrgba8Color buttonDisabled = rgb(0x4C5258, 230);
    UIStraightSrgba8Color scrollBarTrack = rgb(0x07101A, 210);
    UIStraightSrgba8Color scrollBarThumb = rgb(0x58738E, 235);
    UIStraightSrgba8Color scrollBarThumbActive = rgb(0xFFD250, 250);

    float panelBorderWidth = 1.0F;
    float panelShadowOffsetX = 3.0F;
    float panelShadowOffsetY = 4.0F;
    float checkboxIndicatorInset = 6.0F;
    float radioSelectedInset = 6.0F;
    float radioLabelGap = 8.0F;
    float sliderContentInset = 4.0F;
    float sliderThumbWidth = 8.0F;
    float buttonTextSize = 18.0F;
    float bodyTextSize = 18.0F;
    float titleTextSize = 22.0F;

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
    theme.surface2 = rgb(0xE6EBF2);
    theme.borderLight = rgb(0xFFFFFF, 220);
    theme.borderDark = rgb(0x90A0B4, 200);
    theme.textPrimary = rgb(0x1A2430);
    theme.textSecondary = rgb(0x5A6A7C);
    theme.textTitle = rgb(0x0A6A8C);
    theme.textAccent = rgb(0xB07000);
    theme.accent = rgb(0x1A9E6A);
    theme.danger = rgb(0xC03030);
    theme.focusRing = rgb(0x2A70C0, 245);
    theme.shadow = rgb(0x000000, 40);
    theme.buttonNormal = rgb(0x2A8A58);
    theme.buttonDisabled = rgb(0xA0A8B0, 230);
    theme.scrollBarTrack = rgb(0xD2D9E2, 230);
    theme.scrollBarThumb = rgb(0x687B90, 235);
    theme.scrollBarThumbActive = rgb(0x9A6500, 250);
    return theme;
}

[[nodiscard]] constexpr UIBoxPaint makeSolidBox(UIStraightSrgba8Color color) noexcept
{
    return UIBoxPaint{.solidFill = UISolidFill{.color = color}};
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

// Phase A/B panel chrome: fill + optional 1px dual-tone border + optional shadow.
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
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.titleTextSize,
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
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.bodyTextSize,
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
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.bodyTextSize,
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
        .logicalSize = logicalSize > 0.0F ? logicalSize : theme.titleTextSize,
        .advanceScale = 0.65F,
        .lineHeightScale = 1.15F,
        .color = theme.textAccent,
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
    return UIButtonChrome{
        .box = makePanelBoxPaint(theme, fill, UIElevation::Low),
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
                .logicalSize = theme.buttonTextSize,
                .advanceScale = 0.62F,
                .lineHeightScale = 1.15F,
                .color = theme.textPrimary,
            },
    };
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
        .box = makeSolidBox(fill),
        .indicator =
            UICheckboxPaint{
                .checkedIndicatorColor = theme.textPrimary,
                .checkedIndicatorInset = theme.checkboxIndicatorInset,
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

[[nodiscard]] constexpr UIRadioButtonChrome makeRadioButtonChrome(const UITheme& theme) noexcept
{
    return UIRadioButtonChrome{
        .radio =
            UIRadioButtonPaint{
                .indicatorColor = theme.surface2,
                .selectedIndicatorColor = theme.textAccent,
                .selectedIndicatorInset = theme.radioSelectedInset,
                .labelGap = theme.radioLabelGap,
                .focusedIndicatorColor = theme.focusRing,
                .pressedIndicatorColor = darkenChannel(theme.accent, 40),
            },
        .label = makeBodyTextStyle(theme),
    };
}

struct UITextEditChrome final {
    UIBoxPaint box{};
    UITextStyle text{};
};

[[nodiscard]] constexpr UITextEditChrome makeTextEditChrome(const UITheme& theme) noexcept
{
    return UITextEditChrome{
        .box = makeSolidBox(scaleColorAlpha(theme.surface2, 245)),
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
    return UIListViewPaint{
        .scrollBar = makeScrollViewPaint(theme),
        .selectedItemBackgroundColor = scaleColorAlpha(theme.accent, 150),
    };
}

[[nodiscard]] constexpr UITreeViewPaint makeTreeViewPaint(const UITheme& theme) noexcept
{
    return UITreeViewPaint{
        .scrollBar = makeScrollViewPaint(theme),
        .selectedItemBackgroundColor = scaleColorAlpha(theme.accent, 150),
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
    const UIButtonChrome button = makeButtonChrome(theme, scaleColorAlpha(theme.surface2, 245));
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
