#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/ui/UIComponentBuild.hpp>
#include <tina/ui/UIIcon.hpp>
#include <tina/ui/UILayout.hpp>
#include <tina/ui/UINodeId.hpp>

#include <string_view>

namespace Tina::UI {

struct UICollapsibleSectionConfig final {
    std::string_view title{};
    UIIconContent collapsedIndicator{};
    UIIconContent expandedIndicator{};
    UILayoutStyle layout{};
    UILayoutStyle headerLayout{};
    UILayoutStyle indicatorLayout{};
    UILayoutStyle contentLayout{};
    bool expanded = true;
    bool enabled = true;
};

struct UICollapsibleSectionParts final {
    UINodeId root{};
    UINodeId header{};
    UINodeId collapsedIndicator{};
    UINodeId expandedIndicator{};
    UINodeId title{};
    UINodeId content{};

    auto operator<=>(const UICollapsibleSectionParts&) const = default;
};

// One value snapshot keeps the Toggle state and its two dependent presentation
// values coherent. Products apply one snapshot during their retained update.
struct UICollapsibleSectionState final {
    bool headerChecked = false;
    UIVisibility collapsedIndicatorVisibility = UIVisibility::Visible;
    UIVisibility expandedIndicatorVisibility = UIVisibility::Collapsed;
    UIVisibility contentVisibility = UIVisibility::Collapsed;

    auto operator<=>(const UICollapsibleSectionState&) const = default;
};

[[nodiscard]] constexpr UICollapsibleSectionState
synchronizeCollapsibleSectionState(bool expanded) noexcept
{
    return UICollapsibleSectionState{
        .headerChecked = expanded,
        .collapsedIndicatorVisibility = expanded ? UIVisibility::Collapsed
                                                 : UIVisibility::Visible,
        .expandedIndicatorVisibility = expanded ? UIVisibility::Visible
                                                : UIVisibility::Collapsed,
        .contentVisibility = expanded ? UIVisibility::Visible
                                      : UIVisibility::Collapsed,
    };
}

// The header owns the existing Activate + Toggle state. Products use the
// returned content node as the parent for section-specific children.
[[nodiscard]] Core::Result<UIComponentBuildBudget>
requiredCollapsibleSectionBuildBudget(
    const UICollapsibleSectionConfig& config) noexcept;

} // namespace Tina::UI
