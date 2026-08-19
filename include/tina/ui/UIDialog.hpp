#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>
#include <tina/ui/UIButton.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <array>
#include <span>
#include <string_view>

namespace Tina::UI {

inline constexpr usize UIDialogMaximumActionCount = 4;

struct UIDialogActionConfig final {
    std::string_view text{};
    UIButtonVariant variant = UIButtonVariant::Text;
    bool enabled = true;
};

// Modal-based bounded component. Modal remains the sole barrier/focus-scope
// owner; Surface, Title, Body, and action Buttons are ordinary child Elements.
struct UIDialogConfig final {
    std::string_view title{};
    std::string_view body{};
    // Borrowed only for requiredDialogBuildBudget()/buildDialog().
    std::span<const UIDialogActionConfig> actions{};
    UILayoutStyle layout{};
    UILayoutStyle surfaceLayout{};
    float viewportMargin = 24.0F;
};

struct UIDialogParts final {
    UINodeId modal{};
    UINodeId surface{};
    UINodeId title{};
    UINodeId body{};
    UINodeId actionRow{};
    std::array<UINodeId, UIDialogMaximumActionCount> actions{};
    usize actionCount = 0;

    [[nodiscard]] constexpr std::span<const UINodeId> actionButtons() const noexcept
    {
        return std::span<const UINodeId>(actions.data(), actionCount);
    }

    auto operator<=>(const UIDialogParts&) const = default;
};

// Exact fixed reservation. More than UIDialogMaximumActionCount actions, empty
// title/action text, invalid UTF-8, non-finite margin, or invalid variants fail.
[[nodiscard]] Core::Result<UIComponentBuildBudget>
requiredDialogBuildBudget(const UIDialogConfig& config) noexcept;

} // namespace Tina::UI
