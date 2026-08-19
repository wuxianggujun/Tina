#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UIIcon.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UITextEdit.hpp>
#include <tina/ui/UITooltip.hpp>

#include <optional>
#include <string_view>

namespace Tina::UI {

// Optional FormField action. It reuses Button Activate state and the Image/Icon
// render path; the FormField never proxies or duplicates the action callback.
struct UIFormFieldActionConfig final {
    UIIconContent icon{};
    std::string_view accessibleName{};
    std::optional<std::string_view> accessibleDescription{};
    std::optional<std::string_view> tooltipText{};
    UITooltipConfig tooltip{};
    UIButtonVariant variant = UIButtonVariant::Text;
    bool useThemeIconTint = true;
    bool enabled = true;
};

struct UIFormFieldConfig final {
    std::string_view label{};
    std::string_view value{};
    std::optional<std::string_view> helperText{};
    // Presence selects the Invalid TextEdit chrome and becomes the accessible
    // description ahead of helperText. An empty error string still means invalid.
    std::optional<std::string_view> errorText{};
    std::optional<UIFormFieldActionConfig> leadingAction{};
    std::optional<UIFormFieldActionConfig> trailingAction{};
    UITextEditMultilineConfig multiline{};
    UILayoutStyle layout{};
    UILayoutStyle textEditLayout{};
    bool enabled = true;
};

struct UIFormFieldActionParts final {
    UINodeId button{};
    UINodeId icon{};
    UINodeId tooltip{};

    [[nodiscard]] constexpr bool hasValue() const noexcept
    {
        return button.hasValue() && icon.hasValue();
    }

    [[nodiscard]] constexpr bool hasTooltip() const noexcept
    {
        return tooltip.hasValue();
    }

    auto operator<=>(const UIFormFieldActionParts&) const = default;
};

struct UIFormFieldParts final {
    UINodeId root{};
    UINodeId label{};
    UINodeId inputRow{};
    UINodeId textEdit{};
    UINodeId helperText{};
    UINodeId errorText{};
    UIFormFieldActionParts leadingAction{};
    UIFormFieldActionParts trailingAction{};

    auto operator<=>(const UIFormFieldParts&) const = default;
};

// Exact node/text/behavior reservation for buildFormField(). The TextEdit owns
// the only text-input state; each optional action owns one existing Activate slot.
[[nodiscard]] Core::Result<UIComponentBuildBudget>
requiredFormFieldBuildBudget(const UIFormFieldConfig& config) noexcept;

} // namespace Tina::UI
