#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UIColorField.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <array>
#include <span>
#include <string_view>

namespace Tina::UI {

inline constexpr usize UIColorPickerMaximumChannelCount = 4;
inline constexpr usize UIColorPickerChannelTextCapacity = 4;

enum class UIColorPickerChannel : u8 {
    Red = 0,
    Green,
    Blue,
    Alpha,
};

struct UIColorPickerChannelText final {
    std::array<char, UIColorPickerChannelTextCapacity> bytes{};
    usize size = 0;

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view(bytes.data(), size);
    }

    auto operator<=>(const UIColorPickerChannelText&) const = default;
};

[[nodiscard]] constexpr UIColorPickerChannelText
formatColorPickerChannelValue(u8 value) noexcept
{
    UIColorPickerChannelText text{};
    if (value >= 100U)
    {
        text.bytes[text.size++] = static_cast<char>('0' + value / 100U);
    }
    if (value >= 10U)
    {
        text.bytes[text.size++] =
            static_cast<char>('0' + (value / 10U) % 10U);
    }
    text.bytes[text.size++] = static_cast<char>('0' + value % 10U);
    text.bytes[text.size] = '\0';
    return text;
}

struct UIColorPickerState final {
    UIStraightSrgba8Color value{};
    UIColorFieldText text{};
    std::array<float, UIColorPickerMaximumChannelCount> channelValues{};
    std::array<UIColorPickerChannelText, UIColorPickerMaximumChannelCount>
        channelTexts{};
    usize channelCount = 0;

    [[nodiscard]] constexpr std::span<const float> channels() const noexcept
    {
        return std::span<const float>(channelValues.data(), channelCount);
    }

    auto operator<=>(const UIColorPickerState&) const = default;
};

[[nodiscard]] constexpr UIColorPickerState
synchronizeColorPickerValue(UIStraightSrgba8Color color,
                            bool includeAlpha) noexcept
{
    return UIColorPickerState{
        .value = color,
        .text = formatColorFieldValue(color),
        .channelValues = {
            static_cast<float>(color.red),
            static_cast<float>(color.green),
            static_cast<float>(color.blue),
            static_cast<float>(color.alpha),
        },
        .channelTexts = {
            formatColorPickerChannelValue(color.red),
            formatColorPickerChannelValue(color.green),
            formatColorPickerChannelValue(color.blue),
            formatColorPickerChannelValue(color.alpha),
        },
        .channelCount = includeAlpha ? 4U : 3U,
    };
}

// Rebuilds the aggregate color, canonical label, and every channel value in
// one returned snapshot. RGB profiles reject Alpha updates and preserve alpha.
[[nodiscard]] Core::Result<UIColorPickerState>
synchronizeColorPickerChannel(UIStraightSrgba8Color currentValue,
                              bool includeAlpha, UIColorPickerChannel channel,
                              float channelValue) noexcept;

struct UIColorPickerConfig final {
    UIStraightSrgba8Color value{};
    std::array<std::string_view, UIColorPickerMaximumChannelCount>
        channelLabels{"R", "G", "B", "A"};
    std::array<std::string_view, UIColorPickerMaximumChannelCount>
        channelAccessibleNames{"Red", "Green", "Blue", "Alpha"};
    UILayoutStyle layout{};
    bool includeAlpha = true;
    bool enabled = true;
};

struct UIColorPickerParts final {
    UINodeId root{};
    std::array<UINodeId, UIColorPickerMaximumChannelCount> channelRows{};
    std::array<UINodeId, UIColorPickerMaximumChannelCount> channelLabels{};
    std::array<UINodeId, UIColorPickerMaximumChannelCount> channelSliders{};
    std::array<UINodeId, UIColorPickerMaximumChannelCount> channelValueLabels{};
    usize channelCount = 0;

    [[nodiscard]] constexpr std::span<const UINodeId> sliders() const noexcept
    {
        return std::span<const UINodeId>(
            channelSliders.data(), channelCount);
    }

    auto operator<=>(const UIColorPickerParts&) const = default;
};

// Bounded channel editor intended to follow a UIColorField summary. Existing
// Dropdown Popup only accepts DropdownItem children, so this profile does not
// claim popover ownership; products may place it in a Dialog or ordinary layout.
[[nodiscard]] Core::Result<UIComponentBuildBudget>
requiredColorPickerBuildBudget(const UIColorPickerConfig& config) noexcept;

} // namespace Tina::UI
