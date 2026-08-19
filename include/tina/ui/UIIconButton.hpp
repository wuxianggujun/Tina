#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UIIcon.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>
#include <tina/ui/UITooltip.hpp>

#include <optional>
#include <string_view>

namespace Tina::UI {

// Bounded authoring profile composed from an ordinary Button, decorative Icon,
// and optional independent Tooltip. The wrapper only lets Button and Tooltip be
// siblings as required by the Tooltip anti-cycle contract; Button remains the
// sole behavior and accessibility root.
struct UIIconButtonConfig final {
    UIIconContent icon{};
    std::string_view accessibleName{};
    std::optional<std::string_view> accessibleDescription{};
    std::optional<std::string_view> tooltipText{};
    UITooltipConfig tooltip{};
    UIButtonVariant variant = UIButtonVariant::Tonal;
    bool useThemeIconTint = true;
    UILayoutStyle layout{};
    bool enabled = true;
};

struct UIIconButtonParts final {
    UINodeId root{};
    UINodeId button{};
    UINodeId icon{};
    UINodeId tooltip{};

    [[nodiscard]] constexpr bool hasTooltip() const noexcept
    {
        return tooltip.hasValue();
    }

    auto operator<=>(const UIIconButtonParts&) const = default;
};

// Exact fixed-capacity reservation used by buildIconButton(). Invalid text,
// variant, Tooltip config, or an empty accessible name fails before mutation.
[[nodiscard]] Core::Result<UIComponentBuildBudget>
requiredIconButtonBuildBudget(const UIIconButtonConfig& config) noexcept;

} // namespace Tina::UI
