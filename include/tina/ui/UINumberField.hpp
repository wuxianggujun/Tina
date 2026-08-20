#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace Tina::UI {

inline constexpr u8 UINumberFieldMaximumDecimalPlaces = 6;
inline constexpr usize UINumberFieldTextCapacity = 48;

struct UINumberFieldValueSpec final {
    float minValue = 0.0F;
    float maxValue = 100.0F;
    float step = 1.0F;
    u8 decimalPlaces = 0;

    auto operator<=>(const UINumberFieldValueSpec&) const = default;
};

struct UINumberFieldText final {
    std::array<char, UINumberFieldTextCapacity> bytes{};
    usize size = 0;

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view(bytes.data(), size);
    }

    auto operator<=>(const UINumberFieldText&) const = default;
};

struct UINumberFieldState final {
    float value = 0.0F;
    UINumberFieldText text{};

    auto operator<=>(const UINumberFieldState&) const = default;
};

enum class UINumberFieldLabelPlacement : u8 {
    Above = 0,
    Leading,
};

[[nodiscard]] constexpr bool isValidNumberFieldLabelPlacement(
    UINumberFieldLabelPlacement placement) noexcept
{
    return placement == UINumberFieldLabelPlacement::Above ||
           placement == UINumberFieldLabelPlacement::Leading;
}

struct UINumberFieldConfig final {
    std::string_view label{};
    float value = 0.0F;
    UINumberFieldValueSpec valueSpec{};
    std::string_view decrementAccessibleName{"Decrease"};
    std::string_view incrementAccessibleName{"Increase"};
    std::optional<std::string_view> helperText{};
    std::optional<std::string_view> errorText{};
    UINumberFieldLabelPlacement labelPlacement =
        UINumberFieldLabelPlacement::Above;
    UILayoutStyle layout{};
    UILayoutStyle labelLayout{};
    UILayoutStyle textEditLayout{};
    bool enabled = true;
};

struct UINumberFieldParts final {
    UINodeId root{};
    UINodeId label{};
    // Equals root for Above placement; Leading placement owns a content column.
    UINodeId content{};
    UINodeId inputRow{};
    UINodeId decrementButton{};
    UINodeId textEdit{};
    UINodeId incrementButton{};
    UINodeId helperText{};
    UINodeId errorText{};

    auto operator<=>(const UINumberFieldParts&) const = default;
};

// Locale-independent helpers used by both component authoring and product
// callbacks. Parsing consumes the complete string and rejects NaN/Infinity.
[[nodiscard]] Core::Result<float>
normalizeNumberFieldValue(float value, const UINumberFieldValueSpec& spec) noexcept;
[[nodiscard]] Core::Result<float>
parseNumberFieldValue(std::string_view text, const UINumberFieldValueSpec& spec) noexcept;
[[nodiscard]] Core::Result<UINumberFieldText>
formatNumberFieldValue(float value, const UINumberFieldValueSpec& spec) noexcept;
// Each successful synchronization returns the normalized numeric value and its
// canonical fixed-decimal text together; failure produces neither half.
[[nodiscard]] Core::Result<UINumberFieldState>
synchronizeNumberFieldValue(float value,
                            const UINumberFieldValueSpec& spec) noexcept;
[[nodiscard]] Core::Result<UINumberFieldState>
synchronizeNumberFieldText(std::string_view text,
                           const UINumberFieldValueSpec& spec) noexcept;
[[nodiscard]] Core::Result<UINumberFieldState>
stepNumberFieldValue(float value, i32 stepCount,
                     const UINumberFieldValueSpec& spec) noexcept;
[[nodiscard]] Core::Result<UIComponentBuildBudget>
requiredNumberFieldBuildBudget(const UINumberFieldConfig& config) noexcept;

} // namespace Tina::UI
