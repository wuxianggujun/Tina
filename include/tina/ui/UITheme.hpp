#pragma once

#include <tina/ui/UIPaint.hpp>
#include <tina/ui/UIText.hpp>

#include <compare>
#include <optional>

namespace Tina::UI {

// Minimal visual elevation for Phase B fake shadow (SolidQuad only).
enum class UIElevation : u8 {
    None = 0,
    Low = 1,
};

// Thin product Theme tokens (A/B). Not a CSS resolver; callers still use UIBoxPaint
// as escape hatch. Storage remains straight sRGBA8 channels.
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
    UIStraightSrgba8Color shadow = rgb(0x000000, 100);
    float panelBorderWidth = 1.0F;
    float panelShadowOffsetX = 3.0F;
    float panelShadowOffsetY = 4.0F;

    auto operator<=>(const UITheme&) const = default;
};

[[nodiscard]] constexpr UITheme makeDefaultProductTheme() noexcept
{
    return UITheme{};
}

[[nodiscard]] constexpr UIBoxPaint makeSolidBox(UIStraightSrgba8Color color) noexcept
{
    return UIBoxPaint{.solidFill = UISolidFill{.color = color}};
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
    float logicalSize = 22.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize,
        .advanceScale = 0.65F,
        .lineHeightScale = 1.15F,
        .color = theme.textTitle,
    };
}

[[nodiscard]] constexpr UITextStyle makeBodyTextStyle(
    const UITheme& theme,
    float logicalSize = 18.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize,
        .advanceScale = 0.62F,
        .lineHeightScale = 1.12F,
        .color = theme.textPrimary,
    };
}

[[nodiscard]] constexpr UITextStyle makeSecondaryTextStyle(
    const UITheme& theme,
    float logicalSize = 18.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize,
        .advanceScale = 0.62F,
        .lineHeightScale = 1.12F,
        .color = theme.textSecondary,
    };
}

[[nodiscard]] constexpr UITextStyle makeAccentTextStyle(
    const UITheme& theme,
    float logicalSize = 22.0F) noexcept
{
    return UITextStyle{
        .logicalSize = logicalSize,
        .advanceScale = 0.65F,
        .lineHeightScale = 1.15F,
        .color = theme.textAccent,
    };
}

// Deterministic pressed/disabled multipliers for authoring (A).
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

} // namespace Tina::UI
