#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UIPaint.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace Tina::UI {

inline constexpr usize UIColorFieldTextLength = 9;
inline constexpr usize UIColorFieldTextCapacity = UIColorFieldTextLength + 1;

struct UIColorFieldText final {
    std::array<char, UIColorFieldTextCapacity> bytes{};

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view(bytes.data(), UIColorFieldTextLength);
    }

    auto operator<=>(const UIColorFieldText&) const = default;
};

struct UIColorFieldState final {
    UIStraightSrgba8Color value{};
    UIColorFieldText text{};

    auto operator<=>(const UIColorFieldState&) const = default;
};

struct UIColorFieldConfig final {
    std::string_view label{};
    UIStraightSrgba8Color value{};
    std::string_view swatchAccessibleName{"Choose color"};
    std::optional<std::string_view> helperText{};
    std::optional<std::string_view> errorText{};
    UILayoutStyle layout{};
    UILayoutStyle textEditLayout{};
    bool enabled = true;
};

struct UIColorFieldParts final {
    UINodeId root{};
    UINodeId label{};
    UINodeId inputRow{};
    UINodeId swatchButton{};
    UINodeId textEdit{};
    UINodeId helperText{};
    UINodeId errorText{};

    auto operator<=>(const UIColorFieldParts&) const = default;
};

// Canonical form is uppercase ASCII #RRGGBBAA.
[[nodiscard]] constexpr UIColorFieldText
formatColorFieldValue(UIStraightSrgba8Color color) noexcept
{
    constexpr char Digits[] = "0123456789ABCDEF";
    UIColorFieldText text{};
    text.bytes[0] = '#';
    const std::array<u8, 4> channels{
        color.red, color.green, color.blue, color.alpha};
    usize offset = 1;
    for (u8 channel : channels)
    {
        text.bytes[offset++] = Digits[channel >> 4U];
        text.bytes[offset++] = Digits[channel & 0x0FU];
    }
    text.bytes[UIColorFieldTextLength] = '\0';
    return text;
}

[[nodiscard]] constexpr UIColorFieldState
synchronizeColorFieldValue(UIStraightSrgba8Color color) noexcept
{
    return UIColorFieldState{
        .value = color,
        .text = formatColorFieldValue(color),
    };
}

[[nodiscard]] Core::Result<UIStraightSrgba8Color>
parseColorFieldValue(std::string_view text) noexcept;
// Lowercase input is accepted, but the returned text is always canonical.
[[nodiscard]] Core::Result<UIColorFieldState>
synchronizeColorFieldText(std::string_view text) noexcept;
[[nodiscard]] Core::Result<UIComponentBuildBudget>
requiredColorFieldBuildBudget(const UIColorFieldConfig& config) noexcept;

} // namespace Tina::UI
