#pragma once

#include <tina/ui/UIBadge.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UICheckbox.hpp>
#include <tina/ui/UIDataGrid.hpp>
#include <tina/ui/UIDivider.hpp>
#include <tina/ui/UIDropdown.hpp>
#include <tina/ui/UIListView.hpp>
#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIProgressBar.hpp>
#include <tina/ui/UIRadioButton.hpp>
#include <tina/ui/UIScrollView.hpp>
#include <tina/ui/UISlider.hpp>
#include <tina/ui/UISplitView.hpp>
#include <tina/ui/UIStyle.hpp>
#include <tina/ui/UITabView.hpp>
#include <tina/ui/UIText.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UITreeView.hpp>
#include <tina/ui/UIVirtualGridView.hpp>

#include <algorithm>
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
        .caption = 12.0F,
    };
}

[[nodiscard]] constexpr UITypographyScale makeComfortableTypographyScale() noexcept
{
    return UITypographyScale{
        .display = 28.0F,
        .title = 22.0F,
        .section = 18.0F,
        .body = 16.0F,
        .control = 15.0F,
        .caption = 13.0F,
    };
}

enum class UIColorScheme : u8 {
    Dark = 0,
    Light,
};

enum class UIDensity : u8 {
    Compact = 0,
    Comfortable,
};

struct UIColorTokens final {
    UIStraightSrgba8Color background{};
    UIStraightSrgba8Color surface{};
    UIStraightSrgba8Color surfaceContainerLow{};
    UIStraightSrgba8Color surfaceContainer{};
    UIStraightSrgba8Color surfaceContainerHigh{};
    UIStraightSrgba8Color onSurface{};
    UIStraightSrgba8Color onSurfaceVariant{};
    UIStraightSrgba8Color outline{};
    UIStraightSrgba8Color outlineVariant{};
    UIStraightSrgba8Color primary{};
    UIStraightSrgba8Color onPrimary{};
    UIStraightSrgba8Color primaryContainer{};
    UIStraightSrgba8Color onPrimaryContainer{};
    UIStraightSrgba8Color error{};
    UIStraightSrgba8Color onError{};
    UIStraightSrgba8Color errorContainer{};
    UIStraightSrgba8Color onErrorContainer{};
    UIStraightSrgba8Color success{};
    UIStraightSrgba8Color onSuccess{};
    UIStraightSrgba8Color successContainer{};
    UIStraightSrgba8Color onSuccessContainer{};
    UIStraightSrgba8Color warning{};
    UIStraightSrgba8Color onWarning{};
    UIStraightSrgba8Color warningContainer{};
    UIStraightSrgba8Color onWarningContainer{};
    UIStraightSrgba8Color focusRing{};
    UIStraightSrgba8Color scrim{};
    UIStraightSrgba8Color shadow{};

    auto operator<=>(const UIColorTokens&) const = default;
};

struct UIStateLayerTokens final {
    u8 hoveredAlpha = 20;
    u8 focusVisibleAlpha = 26;
    u8 pressedAlpha = 31;
    u8 draggingAlpha = 41;
    u8 disabledContentAlpha = 97;
    u8 disabledContainerAlpha = 31;

    auto operator<=>(const UIStateLayerTokens&) const = default;
};

struct UIShapeTokens final {
    float none = 0.0F;
    float extraSmall = 2.0F;
    float smallRadius = 4.0F;
    float medium = 6.0F;
    float large = 8.0F;
    float full = 999.0F;

    auto operator<=>(const UIShapeTokens&) const = default;
};

struct UISpacingTokens final {
    float space0 = 0.0F;
    float space1 = 2.0F;
    float space2 = 4.0F;
    float space3 = 6.0F;
    float space4 = 8.0F;
    float space5 = 12.0F;
    float space6 = 16.0F;
    float space7 = 20.0F;
    float space8 = 24.0F;
    float space9 = 32.0F;

    auto operator<=>(const UISpacingTokens&) const = default;
};

struct UIElevationTokens final {
    float raisedOffsetY = 1.0F;
    float floatingOffsetY = 2.0F;
    float modalOffsetY = 2.0F;

    auto operator<=>(const UIElevationTokens&) const = default;
};

struct UIControlMetrics final {
    float commandBarHeight = 36.0F;
    float contextToolbarHeight = 32.0F;
    float buttonHeight = 30.0F;
    float iconButtonExtent = 28.0F;
    float iconExtent = 16.0F;
    float textEditHeight = 30.0F;
    float checkboxHitExtent = 28.0F;
    float checkboxMarkExtent = 16.0F;
    float radioIndicatorExtent = 16.0F;
    float switchWidth = 36.0F;
    float switchHeight = 20.0F;
    float sliderHeight = 30.0F;
    float sliderTrackThickness = 4.0F;
    float sliderThumbExtent = 12.0F;
    float progressBarHeight = 4.0F;
    float tabHeight = 30.0F;
    float menuItemHeight = 28.0F;
    float listRowHeight = 26.0F;
    float treeRowHeight = 24.0F;
    float statusBarHeight = 24.0F;
    float splitterHitExtent = 6.0F;
    float splitterLineThickness = 1.0F;
    float tooltipMaxWidth = 320.0F;
    float dialogMinWidth = 420.0F;
    float panelBorderWidth = 1.0F;
    float panelCornerRadius = 6.0F;
    float controlCornerRadius = 4.0F;
    float panelShadowOffsetX = 0.0F;
    float panelShadowOffsetY = 0.0F;
    float checkboxIndicatorInset = 6.0F;
    float switchThumbInset = 3.0F;
    float switchCornerRadius = 10.0F;
    float radioSelectedInset = 6.0F;
    float radioLabelGap = 8.0F;
    float sliderContentInset = 6.0F;
    float dropdownIndicatorWidth = 10.0F;
    float dropdownIndicatorHeight = 6.0F;
    float dropdownIndicatorInset = 10.0F;
    float menuItemIndicatorExtent = 12.0F;
    float menuItemIndicatorGap = 8.0F;
    float menuSeparatorThickness = 1.0F;
    float scrollBarThickness = 10.0F;
    float scrollBarMinThumbExtent = 24.0F;

    auto operator<=>(const UIControlMetrics&) const = default;
};

// Semantic desktop elevation levels. The renderer still consumes one existing
// box paint; these levels only select the committed fill/border/shadow recipe.
enum class UIElevation : u8 {
    Sunken = 0,
    Flat,
    Raised,
    Floating,
    Modal,
};

// Product Theme tokens + control chrome factories. Not a CSS resolver.
// UIContext owns the active theme and can transactionally re-theme existing
// default chrome. Local setBoxPaint / set*Paint / setTextStyle calls detach only
// the corresponding property from later theme updates.
struct UITheme final {
    UIColorScheme colorScheme = UIColorScheme::Dark;
    UIDensity density = UIDensity::Compact;
    UIColorTokens colors{};
    UIStateLayerTokens states{};
    UIShapeTokens shapes{};
    UISpacingTokens spacing{};
    UIElevationTokens elevations{};
    UIControlMetrics controls{};
    UITypographyScale typography = makeCompactTypographyScale();

    auto operator<=>(const UITheme&) const = default;
};

[[nodiscard]] constexpr UITheme makeModernDesktopTheme(
    UIColorScheme scheme = UIColorScheme::Dark,
    UIDensity density = UIDensity::Compact) noexcept
{
    UITheme theme{};
    theme.colorScheme = scheme;
    theme.density = density;
    if (scheme == UIColorScheme::Light) {
        theme.colors = UIColorTokens{
            .background = rgb(0xF5F7FA), .surface = rgb(0xFFFFFF),
            .surfaceContainerLow = rgb(0xF0F3F6), .surfaceContainer = rgb(0xE8EDF2),
            .surfaceContainerHigh = rgb(0xDDE4EA), .onSurface = rgb(0x1B1E23),
            .onSurfaceVariant = rgb(0x555D67), .outline = rgb(0x737D88),
            .outlineVariant = rgb(0xC4CCD4), .primary = rgb(0x1769AA),
            .onPrimary = rgb(0xFFFFFF), .primaryContainer = rgb(0xD4E8FF),
            .onPrimaryContainer = rgb(0x001D35), .error = rgb(0xBA1A1A),
            .onError = rgb(0xFFFFFF), .errorContainer = rgb(0xFFDAD6),
            .onErrorContainer = rgb(0x410002), .success = rgb(0x176B42),
            .onSuccess = rgb(0xFFFFFF), .successContainer = rgb(0xB8F2CF),
            .onSuccessContainer = rgb(0x002111), .warning = rgb(0x765A00),
            .onWarning = rgb(0xFFFFFF), .warningContainer = rgb(0xFFE49A),
            .onWarningContainer = rgb(0x251A00), .focusRing = rgb(0x005EA8, 245),
            .scrim = rgb(0x000000, 102), .shadow = rgb(0x000000, 61),
        };
    } else {
        theme.colors = UIColorTokens{
            .background = rgb(0x101216), .surface = rgb(0x15181D),
            .surfaceContainerLow = rgb(0x1B1F25), .surfaceContainer = rgb(0x22272E),
            .surfaceContainerHigh = rgb(0x2B3139), .onSurface = rgb(0xE8EBF0),
            .onSurfaceVariant = rgb(0xB3BAC4), .outline = rgb(0x78818C),
            .outlineVariant = rgb(0x3E4650), .primary = rgb(0x79B8FF),
            .onPrimary = rgb(0x08233F), .primaryContainer = rgb(0x173B62),
            .onPrimaryContainer = rgb(0xD5E9FF), .error = rgb(0xFFB4AB),
            .onError = rgb(0x690005), .errorContainer = rgb(0x5C2024),
            .onErrorContainer = rgb(0xFFDAD6), .success = rgb(0x62C98F),
            .onSuccess = rgb(0x062316), .successContainer = rgb(0x173D29),
            .onSuccessContainer = rgb(0xB5F3CE), .warning = rgb(0xF2C14E),
            .onWarning = rgb(0x2B1D00), .warningContainer = rgb(0x51400B),
            .onWarningContainer = rgb(0xFFE8A3), .focusRing = rgb(0x9ACBFF),
            .scrim = rgb(0x000000, 153), .shadow = rgb(0x000000, 102),
        };
    }
    if (density == UIDensity::Comfortable) {
        theme.typography = makeComfortableTypographyScale();
        theme.controls = UIControlMetrics{
            .commandBarHeight = 44.0F, .contextToolbarHeight = 40.0F,
            .buttonHeight = 36.0F, .iconButtonExtent = 36.0F, .iconExtent = 20.0F,
            .textEditHeight = 38.0F, .checkboxHitExtent = 36.0F,
            .checkboxMarkExtent = 18.0F, .radioIndicatorExtent = 18.0F,
            .switchWidth = 44.0F, .switchHeight = 24.0F,
            .sliderHeight = 36.0F, .sliderTrackThickness = 4.0F,
            .sliderThumbExtent = 14.0F, .progressBarHeight = 6.0F, .tabHeight = 38.0F,
            .menuItemHeight = 36.0F, .listRowHeight = 34.0F, .treeRowHeight = 32.0F,
            .statusBarHeight = 28.0F, .splitterHitExtent = 8.0F,
            .splitterLineThickness = 1.0F, .tooltipMaxWidth = 360.0F,
            .dialogMinWidth = 480.0F, .panelBorderWidth = 1.0F, .panelCornerRadius = 6.0F,
            .controlCornerRadius = 6.0F, .panelShadowOffsetX = 0.0F, .panelShadowOffsetY = 0.0F,
            .checkboxIndicatorInset = 7.0F, .switchThumbInset = 3.0F,
            .switchCornerRadius = 12.0F, .radioSelectedInset = 7.0F, .radioLabelGap = 8.0F,
            .sliderContentInset = 7.0F, .dropdownIndicatorWidth = 12.0F,
            .dropdownIndicatorHeight = 8.0F, .dropdownIndicatorInset = 12.0F,
            .menuItemIndicatorExtent = 14.0F, .menuItemIndicatorGap = 8.0F,
            .menuSeparatorThickness = 1.0F, .scrollBarThickness = 10.0F,
            .scrollBarMinThumbExtent = 28.0F,
        };
    }
    return theme;
}

// Theme construction is explicit: there are no legacy aliases or alternate
// product factories. Desktop adapters resolve OS preferences before calling it.

[[nodiscard]] constexpr UIStraightSrgba8Color scaleColorAlpha(
    UIStraightSrgba8Color color,
    u8 alpha) noexcept
{
    color.alpha = alpha;
    return color;
}

// Composites `content` over `base` at `alpha`, preserving base alpha. Single entry
// point: the applyStateLayer() that this used to forward to is gone, matching the
// no-alias rule stated above.
[[nodiscard]] constexpr UIStraightSrgba8Color stateLayer(
    UIStraightSrgba8Color base,
    UIStraightSrgba8Color content,
    u8 alpha) noexcept
{
    const auto composite = [alpha](u8 background, u8 foreground) constexpr noexcept -> u8 {
        const u16 value = static_cast<u16>(background) * static_cast<u16>(255U - alpha) +
                          static_cast<u16>(foreground) * static_cast<u16>(alpha);
        return static_cast<u8>((value + 127U) / 255U);
    };
    return rgba8(composite(base.red, content.red), composite(base.green, content.green),
                 composite(base.blue, content.blue), base.alpha);
}

// Product panel chrome: fill + border + optional shadow + rounded outer shape.
[[nodiscard]] constexpr UIBoxPaint makePanelBoxPaint(
    const UITheme& theme,
    UIStraightSrgba8Color fill,
    UIElevation elevation = UIElevation::Flat) noexcept
{
    UIBoxPaint paint{
        .solidFill = UISolidFill{.color = fill},
        .borderLight = theme.colors.outline,
        .borderDark = theme.colors.outlineVariant,
        .borderWidth = theme.controls.panelBorderWidth,
    };
    paint.cornerRadii = UILogicalCornerRadii::uniform(theme.controls.panelCornerRadius);
    if (elevation == UIElevation::Raised || elevation == UIElevation::Floating ||
        elevation == UIElevation::Modal) {
        paint.shadow = theme.colors.shadow;
        paint.shadowOffsetX = theme.controls.panelShadowOffsetX;
        paint.shadowOffsetY = elevation == UIElevation::Modal
                                  ? theme.elevations.modalOffsetY
                                  : (elevation == UIElevation::Floating
                                         ? theme.elevations.floatingOffsetY
                                         : theme.elevations.raisedOffsetY);
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
        .color = theme.colors.onSurface,
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
        .color = theme.colors.onSurface,
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
        .color = theme.colors.onSurfaceVariant,
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
        .color = theme.colors.warning,
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
        .color = theme.colors.onSurface,
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
        .color = theme.colors.onSurface,
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
        .color = theme.colors.onSurfaceVariant,
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
        normalFill.alpha != 0 ? normalFill : scaleColorAlpha(theme.colors.primary, 230);
    UIBoxPaint box = makePanelBoxPaint(theme, fill, UIElevation::Raised);
    box.cornerRadii = UILogicalCornerRadii::uniform(theme.controls.controlCornerRadius);
    box.borderWidth = 0.0F;
    return UIButtonChrome{
        .box = box,
        .states =
            UIButtonPaint{
                .hoveredBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.hoveredAlpha),
                .pressedBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.pressedAlpha),
                .focusedBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.focusVisibleAlpha),
                .disabledBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.disabledContainerAlpha),
                .focusedBorderColor = theme.colors.focusRing,
            },
        .label =
            UITextStyle{
                .logicalSize = theme.typography.control,
                .advanceScale = 0.62F,
                .lineHeightScale = 1.15F,
                .color = theme.colors.onPrimary,
            },
    };
}

[[nodiscard]] constexpr UIButtonChrome makeTonalButtonChrome(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color fill = scaleColorAlpha(theme.colors.surfaceContainer, 245);
    UIBoxPaint box = makePanelBoxPaint(theme, fill, UIElevation::Raised);
    box.cornerRadii = UILogicalCornerRadii::uniform(theme.controls.controlCornerRadius);
    box.borderWidth = 0.0F;
    return UIButtonChrome{
        .box = box,
        .states =
            UIButtonPaint{
                .hoveredBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.hoveredAlpha),
                .pressedBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.pressedAlpha),
                .focusedBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.focusVisibleAlpha),
                .disabledBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.disabledContainerAlpha),
                .focusedBorderColor = theme.colors.focusRing,
            },
        .label = makeBodyTextStyle(theme, theme.typography.control),
    };
}

[[nodiscard]] constexpr UIButtonChrome makeOutlinedButtonChrome(const UITheme& theme) noexcept
{
    UIButtonChrome chrome = makeTonalButtonChrome(theme);
    chrome.box.solidFill = UISolidFill{.color = scaleColorAlpha(theme.colors.surface, 32)};
    chrome.box.borderLight = theme.colors.outline;
    chrome.box.borderDark = theme.colors.outlineVariant;
    chrome.box.borderWidth = theme.controls.panelBorderWidth;
    chrome.states.hoveredBackgroundColor = stateLayer(theme.colors.surface, theme.colors.onSurface, theme.states.hoveredAlpha);
    chrome.states.focusedBackgroundColor = stateLayer(theme.colors.surface, theme.colors.onSurface, theme.states.focusVisibleAlpha);
    chrome.states.pressedBackgroundColor = stateLayer(theme.colors.surface, theme.colors.primary, theme.states.pressedAlpha);
    return chrome;
}

[[nodiscard]] constexpr UIButtonChrome makeTextButtonChrome(const UITheme& theme) noexcept
{
    UIButtonChrome chrome = makeTonalButtonChrome(theme);
    chrome.box = makeSolidBox(scaleColorAlpha(theme.colors.surface, 1), theme.controls.controlCornerRadius);
    chrome.states.hoveredBackgroundColor = stateLayer(theme.colors.surface, theme.colors.onSurface, theme.states.hoveredAlpha);
    chrome.states.focusedBackgroundColor = stateLayer(theme.colors.surface, theme.colors.onSurface, theme.states.focusVisibleAlpha);
    chrome.states.pressedBackgroundColor = stateLayer(theme.colors.surface, theme.colors.primary, theme.states.pressedAlpha);
    chrome.states.disabledBackgroundColor = scaleColorAlpha(theme.colors.surface, 1);
    chrome.label.color = theme.colors.onSurfaceVariant;
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
        boxFill.alpha != 0 ? boxFill : scaleColorAlpha(theme.colors.surfaceContainer, 230);
    return UICheckboxChrome{
        .box = makeSolidBox(fill, theme.controls.controlCornerRadius),
        .indicator =
            UICheckboxPaint{
                .checkedIndicatorColor = theme.colors.onSurface,
                .checkedIndicatorInset = theme.controls.checkboxIndicatorInset,
                .hoveredIndicatorColor = stateLayer(fill, theme.colors.onSurface, theme.states.hoveredAlpha),
                .focusedIndicatorColor = theme.colors.focusRing,
                .pressedIndicatorColor = stateLayer(fill, theme.colors.onSurface, theme.states.pressedAlpha),
            },
    };
}

[[nodiscard]] constexpr UICheckboxChrome makeSwitchChrome(
    const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color uncheckedTrack = scaleColorAlpha(theme.colors.surfaceContainer, 245);
    UIBoxPaint track = makeSolidBox(uncheckedTrack, theme.controls.switchCornerRadius);
    return UICheckboxChrome{
        .box = track,
        .indicator =
            UICheckboxPaint{
                .checkedIndicatorColor = theme.colors.onPrimary,
                .checkedIndicatorInset = theme.controls.switchThumbInset,
                .hoveredIndicatorColor = stateLayer(uncheckedTrack, theme.colors.onSurface, theme.states.hoveredAlpha),
                .focusedIndicatorColor = theme.colors.focusRing,
                .pressedIndicatorColor = stateLayer(uncheckedTrack, theme.colors.onSurface, theme.states.pressedAlpha),
                .uncheckedIndicatorColor = theme.colors.onSurfaceVariant,
                .checkedBackgroundColor = theme.colors.primary,
                .checkedHoveredBackgroundColor = stateLayer(theme.colors.primary, theme.colors.onPrimary, theme.states.hoveredAlpha),
                .checkedFocusedBackgroundColor = stateLayer(theme.colors.primary, theme.colors.onPrimary, theme.states.focusVisibleAlpha),
                .checkedPressedBackgroundColor = stateLayer(theme.colors.primary, theme.colors.onPrimary, theme.states.pressedAlpha),
                .presentation = UIToggleIndicatorPresentation::Switch,
            },
    };
}

struct UIDividerChrome final {
    UIBoxPaint line{};
};

[[nodiscard]] constexpr UIDividerChrome makeDividerChrome(
    const UITheme& theme, UIDividerTone tone = UIDividerTone::Subtle) noexcept
{
    UIStraightSrgba8Color color = scaleColorAlpha(theme.colors.outlineVariant, 150);
    switch (tone)
    {
    case UIDividerTone::Subtle:
        break;
    case UIDividerTone::Strong:
        color = theme.colors.outline;
        break;
    case UIDividerTone::Accent:
        color = scaleColorAlpha(theme.colors.primary, 190);
        break;
    }
    return UIDividerChrome{.line = makeSolidBox(color)};
}

struct UIBadgeChrome final {
    UIBoxPaint box{};
    UITextStyle label{};
};

[[nodiscard]] constexpr UIBadgeChrome makeBadgeChrome(
    const UITheme& theme, UIBadgeTone tone = UIBadgeTone::Neutral) noexcept
{
    UIStraightSrgba8Color fill = scaleColorAlpha(theme.colors.surfaceContainer, 245);
    UIStraightSrgba8Color text = theme.colors.onSurfaceVariant;
    switch (tone)
    {
    case UIBadgeTone::Neutral:
        break;
    case UIBadgeTone::Accent:
        fill = scaleColorAlpha(theme.colors.primaryContainer, 220);
        text = theme.colors.onPrimaryContainer;
        break;
    case UIBadgeTone::Danger:
        fill = scaleColorAlpha(theme.colors.errorContainer, 230);
        text = theme.colors.onErrorContainer;
        break;
    }

    UIBoxPaint box = makeSolidBox(fill, theme.controls.controlCornerRadius);
    UITextStyle label = makeCaptionTextStyle(theme);
    label.color = text;
    return UIBadgeChrome{
        .box = box,
        .label = label,
    };
}

struct UISliderChrome final {
    UIBoxPaint hitSurface{};
    UISliderPaint slider{};
};

[[nodiscard]] constexpr UISliderChrome makeSliderChrome(
    const UITheme& theme,
    UIStraightSrgba8Color filledTrack = {}) noexcept
{
    const UIStraightSrgba8Color filled =
        filledTrack.alpha != 0 ? filledTrack : theme.colors.primary;
    return UISliderChrome{
        .hitSurface = {},
        .slider =
            UISliderPaint{
                .trackColor = scaleColorAlpha(theme.colors.outlineVariant, 230),
                .filledTrackColor = filled,
                .thumbColor = theme.colors.onSurface,
                .hoveredThumbColor = stateLayer(theme.colors.onSurface, theme.colors.primary,
                                                theme.states.hoveredAlpha),
                .draggingThumbColor = stateLayer(theme.colors.primary, theme.colors.onPrimary,
                                                 theme.states.draggingAlpha),
                .focusedThumbColor = theme.colors.focusRing,
                .contentInset = theme.controls.sliderContentInset,
                .trackThickness = theme.controls.sliderTrackThickness,
                .thumbExtent = theme.controls.sliderThumbExtent,
            },
    };
}

struct UISplitterChrome final {
    UISplitterPaint splitter{};
};

[[nodiscard]] constexpr UISplitterChrome makeSplitterChrome(const UITheme& theme) noexcept
{
    return UISplitterChrome{
        .splitter = UISplitterPaint{
            .lineColor = theme.colors.outlineVariant,
            .hoveredLineColor = stateLayer(theme.colors.outlineVariant, theme.colors.primary,
                                           theme.states.hoveredAlpha),
            .draggingLineColor = theme.colors.primary,
            .focusRingColor = theme.colors.focusRing,
            .lineThickness = theme.controls.splitterLineThickness,
            .focusRingThickness = (std::max)(theme.controls.splitterLineThickness, 3.0F),
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
        .track = makeSolidBox(scaleColorAlpha(theme.colors.surfaceContainer, 240)),
        .bar = UIProgressBarPaint{.fillColor = theme.colors.primary},
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
    const UIStraightSrgba8Color base = scaleColorAlpha(theme.colors.surface, 90);
    UIBoxPaint box = makePanelBoxPaint(theme, base, UIElevation::Flat);
    box.cornerRadii = UILogicalCornerRadii::uniform(theme.controls.controlCornerRadius);
    return UITabChrome{
        .box = box,
        .tab =
            UITabPaint{
                .selectedBackgroundColor = scaleColorAlpha(theme.colors.primaryContainer, 255),
                .hoveredBackgroundColor = stateLayer(theme.colors.surfaceContainer, theme.colors.onSurface, theme.states.hoveredAlpha),
                .selectedHoveredBackgroundColor = stateLayer(theme.colors.primaryContainer, theme.colors.onPrimaryContainer, theme.states.hoveredAlpha),
                .focusedBackgroundColor = stateLayer(theme.colors.surfaceContainer, theme.colors.onSurface, theme.states.focusVisibleAlpha),
                .selectedFocusedBackgroundColor = stateLayer(theme.colors.primaryContainer, theme.colors.onPrimaryContainer, theme.states.focusVisibleAlpha),
                .pressedBackgroundColor = stateLayer(theme.colors.surfaceContainer, theme.colors.primary, theme.states.pressedAlpha),
                .selectedPressedBackgroundColor = stateLayer(theme.colors.primaryContainer, theme.colors.onPrimaryContainer, theme.states.pressedAlpha),
                .disabledBackgroundColor = stateLayer(theme.colors.surfaceContainer, theme.colors.onSurface, theme.states.disabledContainerAlpha),
                .focusedBorderColor = theme.colors.focusRing,
            },
        .label = makeBodyTextStyle(theme, theme.typography.control),
    };
}

[[nodiscard]] constexpr UISegmentedButtonChrome makeSegmentedButtonChrome(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color base = scaleColorAlpha(theme.colors.surface, 90);
    UIBoxPaint box = makePanelBoxPaint(theme, base, UIElevation::Flat);
    box.cornerRadii = UILogicalCornerRadii::uniform(theme.controls.controlCornerRadius);
    return UISegmentedButtonChrome{
        .box = box,
        .radio =
            UIRadioButtonPaint{
                .indicatorVisible = false,
                .selectedBackgroundColor = scaleColorAlpha(theme.colors.primaryContainer, 255),
                .hoveredBackgroundColor = stateLayer(theme.colors.surfaceContainer, theme.colors.onSurface, theme.states.hoveredAlpha),
                .selectedHoveredBackgroundColor = stateLayer(theme.colors.primaryContainer, theme.colors.onPrimaryContainer, theme.states.hoveredAlpha),
                .focusedBackgroundColor = stateLayer(theme.colors.surfaceContainer, theme.colors.onSurface, theme.states.focusVisibleAlpha),
                .selectedFocusedBackgroundColor = stateLayer(theme.colors.primaryContainer, theme.colors.onPrimaryContainer, theme.states.focusVisibleAlpha),
                .pressedBackgroundColor = stateLayer(theme.colors.surfaceContainer, theme.colors.primary, theme.states.pressedAlpha),
                .selectedPressedBackgroundColor = stateLayer(theme.colors.primaryContainer, theme.colors.onPrimaryContainer, theme.states.pressedAlpha),
                .disabledBackgroundColor = stateLayer(theme.colors.surfaceContainer, theme.colors.onSurface, theme.states.disabledContainerAlpha),
                .focusedBorderColor = theme.colors.focusRing,
            },
        .label = makeBodyTextStyle(theme, theme.typography.control),
    };
}

[[nodiscard]] constexpr UIRadioButtonChrome makeRadioButtonChrome(const UITheme& theme) noexcept
{
    return UIRadioButtonChrome{
        .radio =
            UIRadioButtonPaint{
                .indicatorColor = theme.colors.surfaceContainer,
                .selectedIndicatorColor = theme.colors.primary,
                .indicatorExtent = theme.controls.radioIndicatorExtent,
                .selectedIndicatorInset = theme.controls.radioSelectedInset,
                .labelGap = theme.controls.radioLabelGap,
                .hoveredIndicatorColor = stateLayer(theme.colors.surfaceContainer, theme.colors.onSurface, theme.states.hoveredAlpha),
                .focusedIndicatorColor = theme.colors.focusRing,
                .pressedIndicatorColor = stateLayer(theme.colors.surfaceContainer, theme.colors.primary, theme.states.pressedAlpha),
                .focusedBorderColor = theme.colors.focusRing,
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
    const UIStraightSrgba8Color fill = scaleColorAlpha(theme.colors.surfaceContainer, 245);
    return UITextEditChrome{
        .box = makeSolidBox(fill, theme.controls.controlCornerRadius),
        .paint =
            UITextEditPaint{
                .hoveredBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.hoveredAlpha),
                .pressedBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.pressedAlpha),
                .focusedBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.focusVisibleAlpha),
                .disabledBackgroundColor = stateLayer(fill, theme.colors.onSurface, theme.states.disabledContainerAlpha),
                .focusedBorderColor = theme.colors.focusRing,
                .selectionBackgroundColor = scaleColorAlpha(theme.colors.focusRing, 190),
                .caretColor = theme.colors.onSurface,
            },
        .text = makeBodyTextStyle(theme, theme.typography.control),
    };
}

[[nodiscard]] constexpr UITextEditChrome makeInvalidTextEditChrome(
    const UITheme& theme) noexcept
{
    UITextEditChrome chrome = makeTextEditChrome(theme);
    chrome.box.borderLight = theme.colors.error;
    chrome.box.borderDark = theme.colors.error;
    chrome.box.borderWidth = theme.controls.panelBorderWidth;
    chrome.paint.focusedBorderColor = theme.colors.error;
    return chrome;
}

[[nodiscard]] constexpr UITextStyle makeErrorTextStyle(const UITheme& theme) noexcept
{
    UITextStyle style = makeSecondaryTextStyle(theme);
    style.color = theme.colors.error;
    return style;
}

struct UISettingsPanelChrome final {
    UIBoxPaint panel{};
};

[[nodiscard]] constexpr UIScrollViewPaint makeScrollViewPaint(const UITheme& theme) noexcept
{
    return UIScrollViewPaint{
        .trackColor = theme.colors.surfaceContainerLow,
        .thumbColor = theme.colors.outlineVariant,
        .draggingThumbColor = theme.colors.primary,
        .thickness = theme.controls.scrollBarThickness,
        .minThumbExtent = theme.controls.scrollBarMinThumbExtent,
    };
}

[[nodiscard]] constexpr UIListViewPaint makeListViewPaint(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color selected = scaleColorAlpha(theme.colors.primaryContainer, 255);
    return UIListViewPaint{
        .scrollBar = makeScrollViewPaint(theme),
        .selectedItemBackgroundColor = selected,
        .hoveredSelectedItemBackgroundColor = stateLayer(selected, theme.colors.onPrimaryContainer, theme.states.hoveredAlpha),
        .focusedSelectedItemBackgroundColor = stateLayer(selected, theme.colors.onPrimaryContainer, theme.states.focusVisibleAlpha),
        .pressedSelectedItemBackgroundColor = stateLayer(selected, theme.colors.onPrimaryContainer, theme.states.pressedAlpha),
    };
}

[[nodiscard]] constexpr UITreeViewPaint makeTreeViewPaint(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color selected = scaleColorAlpha(theme.colors.primaryContainer, 255);
    return UITreeViewPaint{
        .scrollBar = makeScrollViewPaint(theme),
        .selectedItemBackgroundColor = selected,
        .hoveredSelectedItemBackgroundColor = stateLayer(selected, theme.colors.onPrimaryContainer, theme.states.hoveredAlpha),
        .focusedSelectedItemBackgroundColor = stateLayer(selected, theme.colors.onPrimaryContainer, theme.states.focusVisibleAlpha),
        .pressedSelectedItemBackgroundColor = stateLayer(selected, theme.colors.onPrimaryContainer, theme.states.pressedAlpha),
        .disclosureColor = theme.colors.onSurfaceVariant,
    };
}

[[nodiscard]] constexpr UIVirtualGridViewPaint
makeVirtualGridViewPaint(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color selected =
        scaleColorAlpha(theme.colors.primaryContainer, 255);
    return UIVirtualGridViewPaint{
        .scrollBar = makeScrollViewPaint(theme),
        .selectedItemBackgroundColor = selected,
        .hoveredSelectedItemBackgroundColor = stateLayer(
            selected, theme.colors.onPrimaryContainer,
            theme.states.hoveredAlpha),
        .focusedSelectedItemBackgroundColor = stateLayer(
            selected, theme.colors.onPrimaryContainer,
            theme.states.focusVisibleAlpha),
        .pressedSelectedItemBackgroundColor = stateLayer(
            selected, theme.colors.onPrimaryContainer,
            theme.states.pressedAlpha),
    };
}

[[nodiscard]] constexpr UIDataGridPaint
makeDataGridPaint(const UITheme& theme) noexcept
{
    const UIStraightSrgba8Color selected =
        scaleColorAlpha(theme.colors.primaryContainer, 255);
    return UIDataGridPaint{
        .scrollBar = makeScrollViewPaint(theme),
        .columnHeaderBackgroundColor = theme.colors.surfaceContainer,
        .selectedRowBackgroundColor = selected,
        .hoveredSelectedRowBackgroundColor = stateLayer(
            selected, theme.colors.onPrimaryContainer,
            theme.states.hoveredAlpha),
        .focusedSelectedRowBackgroundColor = stateLayer(
            selected, theme.colors.onPrimaryContainer,
            theme.states.focusVisibleAlpha),
        .gridLineColor = theme.colors.outlineVariant,
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
    UIButtonChrome button = makeButtonChrome(theme, scaleColorAlpha(theme.colors.surfaceContainer, 245));
    button.label.color = theme.colors.onSurface;
    return UIDropdownChrome{
        .box = button.box,
        .states = button.states,
        .label = button.label,
        .dropdown =
            UIDropdownPaint{
                .indicatorColor = theme.colors.onSurfaceVariant,
                .openBackgroundColor = stateLayer(theme.colors.primaryContainer,
                                                  theme.colors.onPrimaryContainer,
                                                  theme.states.focusVisibleAlpha),
                .selectedItemBackgroundColor = scaleColorAlpha(theme.colors.primaryContainer, 255),
                .indicatorWidth = theme.controls.dropdownIndicatorWidth,
                .indicatorHeight = theme.controls.dropdownIndicatorHeight,
                .indicatorInset = theme.controls.dropdownIndicatorInset,
            },
    };
}

[[nodiscard]] constexpr UIBoxPaint makePopupBoxPaint(const UITheme& theme) noexcept
{
    return makePanelBoxPaint(theme, scaleColorAlpha(theme.colors.surfaceContainerHigh, 250), UIElevation::Floating);
}

[[nodiscard]] constexpr UIButtonChrome makeDropdownItemChrome(const UITheme& theme) noexcept
{
    UIButtonChrome chrome = makeButtonChrome(theme, scaleColorAlpha(theme.colors.surfaceContainerHigh, 245));
    chrome.box.shadow = {};
    chrome.box.shadowOffsetX = 0.0F;
    chrome.box.shadowOffsetY = 0.0F;
    chrome.label.color = theme.colors.onSurface;
    return chrome;
}

[[nodiscard]] constexpr UISettingsPanelChrome makeSettingsPanelChrome(const UITheme& theme) noexcept
{
    return UISettingsPanelChrome{
        .panel = makePanelBoxPaint(
            theme, scaleColorAlpha(theme.colors.background, 236), UIElevation::Floating),
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
            theme, scaleColorAlpha(theme.colors.surface, 230), UIElevation::Flat),
        .title = makeTitleTextStyle(theme),
        .subtitle = makeAccentTextStyle(theme),
    };
}

} // namespace Tina::UI
